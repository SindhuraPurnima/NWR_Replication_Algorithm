#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <fstream>
#include <sys/shm.h>  // For shared memory
#include <nlohmann/json.hpp>  // JSON parsing library
#include <thread>    // For std::this_thread
#include <chrono>    // For std::chrono
#include "proto/mini2.grpc.pb.h"
#include "proto/mini2.pb.h"
#include "parser/CSV.h"
#include "SpatialAnalysis.h"  // Add this include for query functionality
#include "ReplicationManager.h"

using json = nlohmann::json;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ClientContext;
using grpc::Channel;
using mini2::CollisionData;
using mini2::CollisionBatch;
using mini2::RiskAssessment;
using mini2::Empty;
using mini2::EntryPointService;
using mini2::InterServerService;
using mini2::DatasetInfo;
using mini2::StealRequest;
using mini2::StealResponse;
using mini2::MetricsUpdate;
using mini2::MetricsResponse;

// Structure to hold shared memory information
struct SharedMemorySegment {
    int shmid;
    void* memory;
    size_t size;
};

// Structure to represent a process node in the overlay network
struct ProcessNode {
    std::string id;
    std::string address;
    int port;
    std::vector<std::string> connections;
    SharedMemorySegment shm;
};

// Structure for shared memory control
struct SharedMemoryControl {
    int write_index;
    int read_index;
    int data_count;
    // Additional control fields can be added here
};

// Add these new static tracking variables at the top of the file (global scope)
static int64_t g_total_client_records_sent = 0;  // Total records sent by clients to A
static int64_t g_expected_total_dataset_size = 0;  // Expected final dataset size

// Add a function to set the expected dataset size (could be called via command line)
void setExpectedDatasetSize(int64_t size) {
    g_expected_total_dataset_size = size;
}

// Generic Server implementation
class GenericServer : public EntryPointService::Service, public InterServerService::Service {
private:
    // Server configuration
    std::string server_id;
    std::string server_address;
    int server_port;
    
    // Overlay network configuration
    std::map<std::string, ProcessNode> network_nodes;
    std::vector<std::string> connections;
    
    // Shared memory segments (one for each connection)
    std::map<std::string, SharedMemorySegment> shared_memories;
    
    // gRPC stubs for connections to other servers
    std::map<std::string, std::unique_ptr<InterServerService::Stub>> server_stubs;
    
    // Data processing state
    bool is_entry_point;
    
    // Add counters for data distribution tracking
    int total_records_seen = 0;
    int records_kept_locally = 0;
    std::map<std::string, int> records_forwarded;
    
    // Keep track of node count to help with distribution
    int total_node_count;
    
    // Add SpatialAnalysis instance and local data storage
    SpatialAnalysis spatialAnalysis;
    std::vector<CSVRow> localCollisionData;
    
    // Add to GenericServer class to store dataset size
    int64_t total_dataset_size = 0;
    
    ReplicationManager replication_manager_;
    std::vector<Message> message_queue_;
    
    struct ServerMetrics {
        double cpu_load;
        int queue_size;
        double memory_usage;
        double network_latency;
    } current_metrics_;
    
    // Initialize shared memory for a specific connection
    bool initSharedMemory(const std::string& connection_id, int key, size_t size) {
        SharedMemorySegment shm;
        
        // Create shared memory segment
        shm.size = size;
        shm.shmid = shmget(key, size + sizeof(SharedMemoryControl), IPC_CREAT | 0666);
        if (shm.shmid < 0) {
            std::cerr << "Failed to create shared memory segment for " << connection_id << std::endl;
            return false;
        }
        
        // Attach to shared memory
        shm.memory = shmat(shm.shmid, NULL, 0);
        if (shm.memory == (void*)-1) {
            std::cerr << "Failed to attach to shared memory for " << connection_id << std::endl;
            return false;
        }
        
        // Initialize control structure
        SharedMemoryControl* control = static_cast<SharedMemoryControl*>(shm.memory);
        control->write_index = 0;
        control->read_index = 0;
        control->data_count = 0;
        
        // Add to shared memory map
        shared_memories[connection_id] = shm;
        
        std::cout << "Initialized shared memory for connection to " << connection_id << std::endl;
        return true;
    }
    
    // Initialize gRPC channel to another server
    void initServerStub(const std::string& server_id) {
        if (network_nodes.find(server_id) != network_nodes.end()) {
            std::string target_address = network_nodes[server_id].address + ":" + 
                                        std::to_string(network_nodes[server_id].port);
            auto channel = grpc::CreateChannel(target_address, grpc::InsecureChannelCredentials());
            server_stubs[server_id] = InterServerService::NewStub(channel);
            std::cout << "Created channel to server " << server_id << " at " << target_address << std::endl;
        }
    }
    
    // Forward data to connected servers via gRPC
    void forwardDataToServer(const std::string& server_id, const CollisionBatch& batch) {
        if (server_stubs.find(server_id) == server_stubs.end()) {
            initServerStub(server_id);
        }
        
        ClientContext context;
        Empty response;
        Status status = server_stubs[server_id]->ForwardData(&context, batch, &response);
        
        if (!status.ok()) {
            std::cerr << "Failed to forward data to " << server_id << ": " 
                      << status.error_message() << std::endl;
        }
    }
    
    // Write data to shared memory for a specific connection with retry
    bool writeToSharedMemory(const std::string& connection_id, const CollisionData& data) {
        if (shared_memories.find(connection_id) == shared_memories.end()) {
            std::cerr << "Shared memory for " << connection_id << " not initialized" << std::endl;
            return false;
        }
        
        SharedMemorySegment& shm = shared_memories[connection_id];
        SharedMemoryControl* control = static_cast<SharedMemoryControl*>(shm.memory);
        
        // Try up to 5 times with increasing delays
        for (int attempt = 0; attempt < 5; attempt++) {
        // Check if there's space in the buffer
            if (control->data_count < (shm.size / sizeof(CollisionData))) {
        // Get pointer to data area (after control structure)
        char* data_area = static_cast<char*>(shm.memory) + sizeof(SharedMemoryControl);
        
        // Serialize the data to the appropriate position
        std::string serialized_data;
        data.SerializeToString(&serialized_data);
        
        // Copy serialized data to shared memory
        std::memcpy(data_area + control->write_index * sizeof(CollisionData), 
                   serialized_data.data(), 
                   std::min(serialized_data.size(), sizeof(CollisionData)));
        
        // Update control structure
        control->write_index = (control->write_index + 1) % (shm.size / sizeof(CollisionData));
        control->data_count++;
        
        return true;
            }
            
            // Buffer is full, log only on first attempt to avoid spam
            if (attempt == 0) {
                std::cerr << "Shared memory buffer full for " << connection_id << ", retrying..." << std::endl;
            }
            
            // Wait with exponential backoff before retrying (10ms, 20ms, 40ms, 80ms, 160ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(10 * (1 << attempt)));
        }
        
        std::cerr << "Shared memory buffer persistently full for " << connection_id << 
                  ", falling back to gRPC" << std::endl;
        return false;
    }
    
    // Read data from shared memory for a specific connection
    bool readFromSharedMemory(const std::string& connection_id, CollisionData& data) {
        if (shared_memories.find(connection_id) == shared_memories.end()) {
            std::cerr << "Shared memory for " << connection_id << " not initialized" << std::endl;
            return false;
        }
        
        SharedMemorySegment& shm = shared_memories[connection_id];
        SharedMemoryControl* control = static_cast<SharedMemoryControl*>(shm.memory);
        
        // Check if there's data to read
        if (control->data_count == 0) {
            return false;
        }
        
        // Get pointer to data area (after control structure)
        char* data_area = static_cast<char*>(shm.memory) + sizeof(SharedMemoryControl);
        
        // Read serialized data from the appropriate position
        std::string serialized_data;
        serialized_data.resize(sizeof(CollisionData));
        std::memcpy(serialized_data.data(), 
                   data_area + control->read_index * sizeof(CollisionData),
                   sizeof(CollisionData));
        
        // Deserialize the data
        data.ParseFromString(serialized_data);
        
        // Update control structure
        control->read_index = (control->read_index + 1) % (shm.size / sizeof(CollisionData));
        control->data_count--;
        
        return true;
    }
    
    // Add ranking calculation
    double calculateRank(const ServerMetrics& metrics) {
        return (
            config_.weights.queue_size * normalizeQueueSize(metrics.queue_size) +
            config_.weights.cpu_load * metrics.cpu_load +
            config_.weights.network_latency * normalizeLatency(metrics.network_latency)
        );
    }

    // Replace Mini 2's equal distribution with this:
    std::string chooseTargetServer(const CollisionData& data) {
        std::map<std::string, double> serverRanks;
        
        // Calculate ranks for all servers
        for (const auto& [serverId, node] : network_nodes) {
            ServerMetrics metrics = getServerMetrics(serverId);
            serverRanks[serverId] = calculateRank(metrics);
        }
        
        // Find server with best rank
        std::string bestServer = server_id;
        double bestRank = serverRanks[server_id];
        
        for (const auto& [serverId, rank] : serverRanks) {
            if (rank > bestRank) {
                bestRank = rank;
                bestServer = serverId;
            }
        }
        
        return bestServer;
    }
    
    // Update the reporting method
    void reportEnhancedDistributionStats() {
        // Use global value if class variable isn't set but global is
        if (total_dataset_size == 0 && g_expected_total_dataset_size > 0) {
            total_dataset_size = g_expected_total_dataset_size;
        }
        
        std::cout << "\n--- ENHANCED DATA DISTRIBUTION STATISTICS ---\n";
        std::cout << "Server: " << server_id << std::endl;
        std::cout << "Total records seen by this server: " << total_records_seen << std::endl;
        
        if (total_records_seen > 0) {
            double keep_percentage = (records_kept_locally * 100.0) / total_records_seen;
            std::cout << "Records kept locally: " << records_kept_locally 
                      << " (" << keep_percentage << "% of records seen by this server)" << std::endl;
            
            // Show percentage of total dataset for ALL servers
            if (total_dataset_size > 0) {
                double global_percentage = (records_kept_locally * 100.0) / total_dataset_size;
                std::cout << "Records kept locally as % of total dataset: " 
                          << global_percentage << "%" << std::endl;
                
                // Expected ideal value is 100% / total_node_count
                double ideal_percentage = 100.0 / total_node_count;
                std::cout << "Ideal distribution: " << ideal_percentage << "% per node" << std::endl;
                
                // Show variance from ideal
                double variance = global_percentage - ideal_percentage;
                std::cout << "Variance from ideal: " << variance << "% (" 
                          << (variance > 0 ? "over-allocated" : "under-allocated") << ")" << std::endl;
            }
            
            // For entry point, show global processing progress
            if (is_entry_point) {
                double progress = (total_records_seen * 100.0) / total_dataset_size;
                std::cout << "Processing progress: " << progress << "% complete" << std::endl;
            }
            
            // Regular forwarding stats
            std::cout << "Records forwarded:" << std::endl;
            for (const auto& stat : records_forwarded) {
                double forward_percentage = (stat.second * 100.0) / total_records_seen;
                std::cout << "  To " << stat.first << ": " << stat.second 
                          << " (" << forward_percentage << "%)" << std::endl;
            }
        }
        
        std::cout << "--- END ENHANCED STATISTICS ---\n\n";
    }

    // Helper method to estimate total records across all servers (this is approximate)
    int64_t estimateTotalRecordsAllServers() {
        // If we're the entry point, we've seen everything that entered the system
        if (is_entry_point) {
            return total_records_seen;
        } else {
            // For non-entry point, try to guess based on hash distribution
            // This is inherently imprecise without global coordination
            if (records_kept_locally > 0) {
                // Assuming ideal distribution, extrapolate from our local data
                return records_kept_locally * total_node_count;
            } else {
                return 0;  // Can't estimate if we have no data
            }
        }
    }

    // Helper method to estimate this server's share of global data
    double estimateGlobalPercentage() {
        int64_t estimated_total = estimateTotalRecordsAllServers();
        if (estimated_total > 0) {
            return (records_kept_locally * 100.0) / estimated_total;
        }
        return 0.0;
    }

    // Add a method to analyze network topology
    void analyzeNetworkTopology() {
        std::cout << "\n--- Network Topology Analysis for Server " << server_id << " ---\n";
        
        // Determine node types in the network
        std::set<std::string> entry_points;
        std::set<std::string> intermediary_nodes;
        std::set<std::string> leaf_nodes;
        
        // Count incoming connections for each node
        std::map<std::string, int> incoming_connections;
        
        // Initialize counts
        for (const auto& node_pair : network_nodes) {
            incoming_connections[node_pair.first] = 0;
        }
        
        // Count incoming connections
        for (const auto& node_pair : network_nodes) {
            for (const auto& conn : node_pair.second.connections) {
                incoming_connections[conn]++;
            }
        }
        
        // Classify nodes
        for (const auto& node_pair : network_nodes) {
            const std::string& node_id = node_pair.first;
            const auto& connections = node_pair.second.connections;
            
            if (node_pair.second.id == server_id && is_entry_point) {
                entry_points.insert(node_id);
            } else if (connections.empty()) {
                leaf_nodes.insert(node_id);
            } else {
                intermediary_nodes.insert(node_id);
            }
        }
        
        // Log topology information
        std::cout << "  Entry points: ";
        for (const auto& node : entry_points) std::cout << node << " ";
        std::cout << "\n";
        
        std::cout << "  Intermediary nodes: ";
        for (const auto& node : intermediary_nodes) std::cout << node << " ";
        std::cout << "\n";
        
        std::cout << "  Leaf nodes: ";
        for (const auto& node : leaf_nodes) std::cout << node << " ";
        std::cout << "\n";
        
        std::cout << "  Connection map:\n";
        for (const auto& node_pair : network_nodes) {
            std::cout << "    " << node_pair.first << " → ";
            if (node_pair.second.connections.empty()) {
                std::cout << "(endpoint)";
            } else {
                for (const auto& conn : node_pair.second.connections) {
                    std::cout << conn << " ";
                }
            }
            std::cout << " (incoming: " << incoming_connections[node_pair.first] << ")\n";
        }
        
        std::cout << "--- End of Network Analysis ---\n\n";
    }

    // Convert CollisionData (protobuf) to CSVRow for analysis
    CSVRow convertToCSVRow(const CollisionData& data) {
        CSVRow row;
        row.crash_date = data.crash_date();
        row.crash_time = data.crash_time();
        row.borough = data.borough();
        row.zip_code = data.zip_code().empty() ? 0 : std::stoi(data.zip_code());
        row.latitude = data.latitude();
        row.longitude = data.longitude();
        row.location = data.location();
        row.on_street_name = data.on_street_name();
        row.cross_street_name = data.cross_street_name();
        row.off_street_name = data.off_street_name();
        row.persons_injured = data.number_of_persons_injured();
        row.persons_killed = data.number_of_persons_killed();
        row.pedestrians_injured = data.number_of_pedestrians_injured();
        row.pedestrians_killed = data.number_of_pedestrians_killed();
        row.cyclists_injured = data.number_of_cyclist_injured();
        row.cyclists_killed = data.number_of_cyclist_killed();
        row.motorists_injured = data.number_of_motorist_injured();
        row.motorists_killed = data.number_of_motorist_killed();
        
        // Additional fields would be set here
        return row;
    }
    
    // Process local data using SpatialAnalysis
    void processLocalData() {
        if (localCollisionData.empty()) {
            std::cout << "No local data to analyze on server " << server_id << std::endl;
            return;
        }
        
        std::cout << "\n--- PERFORMING SPATIAL ANALYSIS ON SERVER " << server_id << " ---\n";
        std::cout << "Processing " << localCollisionData.size() << " collision records\n";
        
        // Process the data with SpatialAnalysis
        spatialAnalysis.processCollisions(localCollisionData);
        
        // Identify and print high-risk areas
        spatialAnalysis.identifyHighRiskAreas();
        
        std::cout << "--- END OF SPATIAL ANALYSIS ---\n\n";
    }

public:
    GenericServer(const std::string& config_path) 
        : is_entry_point(false),
          spatialAnalysis(10, 2)  // 10 injuries or 2 deaths to mark area high-risk
    {
        // Use the global value if it's been set via command line
        if (g_expected_total_dataset_size > 0) {
            total_dataset_size = g_expected_total_dataset_size;
            std::cout << "Using dataset size from command line: " 
                      << total_dataset_size << " records" << std::endl;
        }
        
        // Load configuration from JSON file
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            std::cerr << "Failed to open config file: " << config_path << std::endl;
            exit(1);
        }
        
        json config;
        config_file >> config;
        
        // Parse server configuration
        server_id = config["server_id"];
        server_address = config["address"];
        server_port = config["port"];
        is_entry_point = config["is_entry_point"];
        
        std::cout << "Configuring server " << server_id 
                  << " at " << server_address << ":" << server_port << std::endl;
        
        // Parse network configuration
        for (const auto& node : config["network"]) {
            ProcessNode process_node;
            process_node.id = node["id"];
            process_node.address = node["address"];
            process_node.port = node["port"];
            
            // Parse connections for this node
            for (const auto& conn : node["connections"]) {
                process_node.connections.push_back(conn);
            }
            
            network_nodes[process_node.id] = process_node;
            
            // If this is the current server, set up its connections
            if (process_node.id == server_id) {
                connections = process_node.connections;
            }
        }
        
        // Analyze the network topology to understand the structure
        analyzeNetworkTopology();
        
        // Initialize shared memory for each connection
        int base_key = 1000;  // Starting key for shared memory
        for (size_t i = 0; i < connections.size(); i++) {
            std::string conn_id = connections[i];
            
            // Determine if the connection is on the same machine by comparing IP addresses
            // Two processes are on the same machine if they have the same IP address
            bool is_local = (network_nodes[conn_id].address == network_nodes[server_id].address);
            
            // If not local, skip shared memory setup
            if (!is_local) {
                std::cout << "Connection to " << conn_id << " is remote (" 
                          << network_nodes[conn_id].address << " vs " 
                          << network_nodes[server_id].address << "), using gRPC only." << std::endl;
                continue;
            }
            
            std::cout << "Connection to " << conn_id << " is local (same address: " 
                      << network_nodes[conn_id].address << "), using shared memory." << std::endl;
            
            // Use a different key for each connection
            int key = base_key + i;
            // Increase from 1MB to 20MB shared memory segment for each connection
            if (!initSharedMemory(conn_id, key, 20 * 1024 * 1024)) {
                std::cerr << "Failed to initialize shared memory for " << conn_id << std::endl;
                // Don't exit, just continue with gRPC only
            }
        }
        
        // Initialize gRPC stubs for all connections
        for (const auto& conn : connections) {
            initServerStub(conn);
        }
        
        // After parsing network configuration
        total_node_count = network_nodes.size();
        std::cout << "Network has " << total_node_count << " nodes" << std::endl;
    }
    
    ~GenericServer() {
        // Run final spatial analysis on locally stored data
        processLocalData();
        
        // Clean up shared memory
        for (auto& shm_pair : shared_memories) {
            if (shm_pair.second.memory != nullptr && shm_pair.second.memory != (void*)-1) {
                shmdt(shm_pair.second.memory);
                shmctl(shm_pair.second.shmid, IPC_RMID, NULL);
            }
        }
    }
    
    // Handle incoming collision data from Python client (entry point)
    Status StreamCollisions(ServerContext* context,
                          grpc::ServerReader<CollisionData>* reader,
                          Empty* response) override {
        // Only process this if the server is an entry point
        if (!is_entry_point) {
            return Status(grpc::StatusCode::FAILED_PRECONDITION, 
                         "This server is not configured as an entry point");
        }
        
        CollisionData collision;
        int count = 0;
        std::map<std::string, int> routing_stats;
        
        // Add at the beginning:
        static int entry_count = 0;
        
        // Read streaming data from client
        while (reader->Read(&collision)) {
            count++;
            entry_count++;  // Count all entries ever received
            
            // Increment total records seen for ALL records at entry point
            total_records_seen++;
            
            // Log progress
            if (count % 100 == 0) {
                std::cout << "Received " << count << " records" << std::endl;
            }
            
            // Determine if this server should keep the data locally
            bool keep_locally = shouldKeepLocally(collision);
            
            if (keep_locally) {
                records_kept_locally++;
                
                // Convert and store for SpatialAnalysis
                localCollisionData.push_back(convertToCSVRow(collision));
                
                // No target server, store locally
                if (count % 100 == 0) {
                    std::cout << "Data kept locally on entry point server" << std::endl;
                }
                continue;
            }
            
            // Otherwise, determine which server to route this data to
            std::string target_server = chooseTargetServer(collision);
            
            if (!target_server.empty()) {
                // Update routing statistics
                routing_stats[target_server]++;
                records_forwarded[target_server]++;
                
                // Check if target is on local machine for shared memory
                bool is_local = (network_nodes[target_server].address == network_nodes[server_id].address);
                
                // Try to write to shared memory if local
                if (is_local && shared_memories.find(target_server) != shared_memories.end() && 
                    writeToSharedMemory(target_server, collision)) {
                    // Successfully used shared memory
                    if (count % 500 == 0) {  // Reduce log spam
                    std::cout << "Data written to shared memory for " << target_server << std::endl;
                    }
                } else {
                    // Use gRPC
                    CollisionBatch batch;
                    *batch.add_collisions() = collision;
                    forwardDataToServer(target_server, batch);
                    
                    if (count % 500 == 0) {  // Reduce log spam
                        std::cout << "Data sent via gRPC to " << target_server << std::endl;
                    }
                }
            } else {
                // No available connection but we should have forwarded - rare case
                records_kept_locally++;
                if (count % 100 == 0) {
                    std::cout << "No route available, keeping locally" << std::endl;
                }
            }
            
            // Add periodic reporting similar to what other servers use
            if (entry_count % 1000 == 0) {  // Every 1000 records (adjust as needed)
                std::cout << "\n--- PERIODIC DATA DISTRIBUTION REPORT (ENTRY POINT) ---\n";
                reportEnhancedDistributionStats();
                std::cout << "--- END PERIODIC REPORT ---\n\n";
            }
        }
        
        // Print routing statistics
        std::cout << "Finished receiving " << count << " records" << std::endl;
        std::cout << "Routing statistics:" << std::endl;
        for (const auto& stat : routing_stats) {
            std::cout << "  Sent to " << stat.first << ": " << stat.second << " records" << std::endl;
        }
        
        // Report distribution stats and perform analysis after processing all records
        reportEnhancedDistributionStats();
        processLocalData();
        
        return Status::OK;
    }
    
    // Handle forwarded data from other servers
    Status ForwardData(ServerContext* context, const CollisionBatch& batch, Empty* response) {
        for (const auto& data : batch.data()) {
            // Instead of round-robin, use ranking
            std::string targetServer = chooseTargetServer(data);
            
            if (targetServer != server_id) {
                forwardToServer(targetServer, data);
            } else {
                processLocally(data);
            }
        }
        return Status::OK;
    }
    
    // Handle sharing of analysis results
    Status ShareAnalysis(ServerContext* context,
                        const RiskAssessment* assessment,
                        Empty* response) override {
        std::cout << "Received risk assessment for " << assessment->borough() 
                  << " " << assessment->zip_code() << std::endl;
        
        // Process the assessment data
        // ...
        
        return Status::OK;
    }
    
    // Get server address (IP:port)
    std::string getServerAddress() const {
        return server_address + ":" + std::to_string(server_port);
    }
    
    // Check if this server is an entry point
    bool isEntryPoint() const {
        return is_entry_point;
    }

    // Add a new RPC implementation
    Status SetDatasetInfo(ServerContext* context,
                         const DatasetInfo* info,
                         Empty* response) override {
        total_dataset_size = info->total_size();
        std::cout << "Received dataset size information: " << total_dataset_size << " records" << std::endl;
        
        // Forward this information to all connected servers
        broadcastDatasetSize();
        
        return Status::OK;
    }
    
    grpc::Status StealWork(grpc::ServerContext* context,
                          const StealRequest* request,
                          StealResponse* response) override {
        auto stolen_messages = replication_manager_.handleStealRequest(
            request->requested_items());
        *response->mutable_stolen_messages() = {
            stolen_messages.begin(), 
            stolen_messages.end()
        };
        return grpc::Status::OK;
    }

    grpc::Status UpdateMetrics(grpc::ServerContext* context,
                             const MetricsUpdate* request,
                             MetricsResponse* response) override {
        // Update metrics
        current_metrics_.cpu_load = getCpuLoad();
        current_metrics_.queue_size = message_queue_.size();
        current_metrics_.memory_usage = getMemoryUsage();
        current_metrics_.network_latency = measureAverageLatency();
        return grpc::Status::OK;
    }

private:
    // Add a method to forward dataset size to all connected servers
    void broadcastDatasetSize() {
        // Only entry point should broadcast the total size
        if (!is_entry_point) return;
        
        // Create RPC message
        DatasetInfo info;
        info.set_total_size(total_dataset_size);
        
        // Send to each connected server
        for (const std::string& server_id : connections) {
            if (server_stubs.find(server_id) == server_stubs.end()) {
                initServerStub(server_id);
            }
            
            ClientContext context;
            Empty response;
            
            // We need to add this RPC to the InterServerService too
            Status status = server_stubs[server_id]->SetTotalDatasetSize(&context, info, &response);
            
            if (status.ok()) {
                std::cout << "Forwarded dataset size to server " << server_id << std::endl;
            } else {
                std::cerr << "Failed to forward dataset size to " << server_id << std::endl;
            }
        }
    }

    // Add a new RPC implementation
    Status SetTotalDatasetSize(ServerContext* context,
                              const DatasetInfo* info,
                              Empty* response) override {
        total_dataset_size = info->total_size();
        std::cout << "Received total dataset size from another server: " 
                  << total_dataset_size << " records" << std::endl;
        
        // Forward to our connections (creates a broadcast tree)
        for (const std::string& server_id : connections) {
            // Skip the one we received from (to avoid loops)
            std::string peer = context->peer();
            if (peer.find(network_nodes[server_id].address) != std::string::npos) {
                continue;
            }
            
            if (server_stubs.find(server_id) == server_stubs.end()) {
                initServerStub(server_id);
            }
            
            ClientContext new_context;
            Empty new_response;
            Status status = server_stubs[server_id]->SetTotalDatasetSize(&new_context, *info, &new_response);
            
            if (status.ok()) {
                std::cout << "Forwarded dataset size to server " << server_id << std::endl;
            } else {
                std::cerr << "Failed to forward dataset size to " << server_id << std::endl;
            }
        }
        
        return Status::OK;
    }

    // New Mini 3 routing logic
    bool shouldKeepLocally(const CollisionData& data) {
        std::string bestServer = chooseTargetServer(data);
        return bestServer == server_id;
    }

    void updateMetrics() {
        current_metrics_.cpu_load = getCpuLoad();
        current_metrics_.queue_size = message_queue_.size();
        current_metrics_.memory_usage = getMemoryUsage();
        current_metrics_.network_latency = measureAverageLatency();
    }
};

class ReplicationServer final : public ReplicationService::Service {
public:
    explicit ReplicationServer(const std::string& config_path) 
        : replication_mgr_(loadConfig(config_path)) {
        // Initialize server
    }

    Status RouteMessage(ServerContext* context, const Message* request,
                       Response* response) override {
        bool handled = replication_mgr_.handleMessage(*request);
        response->set_success(handled);
        return Status::OK;
    }

    Status StealWork(ServerContext* context, const StealRequest* request,
                    StealResponse* response) override {
        auto stolen = replication_mgr_.handleStealRequest(request->requested_items());
        *response->mutable_stolen_messages() = {stolen.begin(), stolen.end()};
        return Status::OK;
    }

    Status UpdateMetrics(ServerContext* context, const MetricsUpdate* request,
                        MetricsResponse* response) override {
        replication_mgr_.updateMetrics();
        return Status::OK;
    }

private:
    ReplicationManager replication_mgr_;
};

int main(int argc, char** argv) {
    // Check for config file path
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file.json> [expected_dataset_size]" << std::endl;
        return 1;
    }
    
    std::string config_path = argv[1];
    
    // Check for optional expected dataset size
    if (argc >= 3) {
        try {
            int64_t expected_size = std::stoll(argv[2]);
            g_expected_total_dataset_size = expected_size;
            std::cout << "Expected total dataset size: " << g_expected_total_dataset_size << " records" << std::endl;
        } catch (...) {
            std::cerr << "Invalid expected dataset size, ignoring" << std::endl;
        }
    }
    
    // Create and configure the server
    GenericServer server(config_path);
    
    // Set up gRPC server
    ServerBuilder builder;
    builder.AddListeningPort(server.getServerAddress(), grpc::InsecureServerCredentials());

    // Explicitly cast to resolve the ambiguity
    if (server.isEntryPoint()) {
        builder.RegisterService(static_cast<EntryPointService::Service*>(&server));
    }
    builder.RegisterService(static_cast<InterServerService::Service*>(&server));
    
    // Start the server
    std::unique_ptr<Server> grpc_server(builder.BuildAndStart());
    std::cout << "Server " << (server.isEntryPoint() ? "(entry point) " : "") 
              << "listening on " << server.getServerAddress() << std::endl;
    
    // Wait for server to finish
    grpc_server->Wait();
    
    return 0;
}