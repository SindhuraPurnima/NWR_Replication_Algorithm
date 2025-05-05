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
#include "MetricsCollector.h"  // Include MetricsCollector header which has ServerMetrics
#include "ReplicationManager.h"
#include <algorithm>
#include <cmath>

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
using mini2::InterServerService;
using mini2::DatasetInfo;
using mini2::StealRequest;
using mini2::StealResponse;
using mini2::MetricsUpdate;
using mini2::MetricsResponse;
using namespace mini2;

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

// Add missing helper functions
double getCpuLoad() {
    // Simple implementation
    return 0.5; // 50% load for testing
}

double getMemoryUsage() {
    // Simple implementation
    return 0.4; // 40% usage for testing
}

double measureAverageLatency() {
    // Simple implementation
    return 50.0; // 50ms for testing
}

double normalizeQueueSize(int size) {
    const int MAX_QUEUE_SIZE = 1000;
    return static_cast<double>(size) / MAX_QUEUE_SIZE;
}

double normalizeLatency(double latency) {
    const double MAX_LATENCY = 1000.0;
    return std::min(1.0, latency / MAX_LATENCY);
}

double normalizeDistance(double distance) {
    const double MAX_DISTANCE = 10.0;  // Maximum network distance
    return std::min(1.0, distance / MAX_DISTANCE);
}

ServerMetrics getServerMetrics(const std::string& serverId) {
    ServerMetrics metrics;
    metrics.set_cpu_utilization(getCpuLoad());
    metrics.set_message_queue_length(0);
    metrics.set_memory_usage(getMemoryUsage());
    metrics.set_avg_network_latency(measureAverageLatency());
    metrics.set_max_queue_length(1000);  // Configurable
    return metrics;
}

// Add this before the GenericServer class definition
ReplicationManager::Config loadConfig(const std::string& config_path) {
    ReplicationManager::Config config;
    config.server_id = "server1";
    config.address = "0.0.0.0";
    config.port = 50051;
    config.max_queue_size = 1000;
    config.stealing_threshold = 0.7;
    config.stealing_batch_size = 10;
    config.metrics_interval_ms = 1000;
    config.stealing_interval_ms = 5000;
    config.min_replicas = 3;
    config.max_steal_attempts = 3;
    config.max_steal_hops = 2;
    config.steal_cooldown_ms = 1000;

    // Initialize weights
    ReplicationManager::Weights weights;
    weights.queue_size = 0.4;
    weights.cpu_load = 0.3;
    weights.memory_usage = 0.2;
    weights.network_latency = 0.3;
    weights.distance = 0.1;

    return config;
}

// Generic Server implementation
class GenericServer : public InterServerService::Service {
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
    std::vector<CollisionData> message_queue_;
    ReplicationManager::Config config_;  // Add this line
    
    // Replace the ServerMetrics struct with ReplicationManager's version
    ServerMetrics current_metrics_;
    
    MetricsCollector metrics_collector_;
    std::vector<ServerMetrics> metrics_history_;
    
    // Replication state (N: number of replicas, W: write quorum, R: read quorum)
    int N = 3;  // Number of replicas
    int W = 2;  // Write quorum
    int R = 2;  // Read quorum
    
    // Replication state
    std::vector<CollisionData> local_queue;
    std::map<std::string, double> server_ranks;  // For load balancing
    std::map<std::string, int> hop_counts;       // For distance tracking
    
    // Metrics
    double cpu_load = 0.0;
    double network_latency = 0.0;
    int queue_size = 0;
    int max_queue_size = 1000;  // Configurable capacity
    
    // Work stealing parameters
    int max_steal_distance = 3;  // Maximum hops for stealing
    int min_queue_size = 100;    // Minimum queue size before stealing
    double steal_threshold = 0.7; // CPU load threshold for stealing
    
    // Add weights_ as a member variable
    ReplicationManager::Weights weights_;
    
    // Add this helper function to calculate distance between servers
    double calculateDistance(const std::string& target_server_id) {
        // Simple implementation: return 1.0 for now
        // In a real implementation, this would calculate network distance
        return 1.0;
    }
    
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
        metrics_collector_.recordRecordForwarded(server_id);
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
    
    // Update the calculateRank function to use weights_
    double calculateRank(const ServerMetrics& metrics) {
        double score = 
            weights_.queue_size * normalizeQueueSize(metrics.message_queue_length()) +
            weights_.cpu_load * metrics.cpu_utilization() +
            weights_.memory_usage * metrics.memory_usage() +
            weights_.network_latency * normalizeLatency(metrics.avg_network_latency()) +
            weights_.distance * normalizeDistance(calculateDistance(server_id));
        return score;
    }

    // Replace Mini 2's equal distribution with this:
    std::string chooseTargetServer(const CollisionData& data) {
        std::map<std::string, double> serverRanks;
        
        // Calculate ranks for all servers
        for (const auto& [serverId, node] : network_nodes) {
            auto metrics = getServerMetrics(serverId);
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

    // Update analyzeNetworkTopology to use weights_
    void analyzeNetworkTopology() {
        std::cout << "\n=== Server " << server_id << " Initialization Report ===\n";
        std::cout << "Address: " << config_.address << ":" << config_.port << "\n\n";
        
        std::cout << "--- Replication Configuration ---\n";
        std::cout << "Min Replicas: " << config_.min_replicas << "\n";
        std::cout << "Work Stealing Threshold: " << config_.stealing_threshold << "\n";
        std::cout << "Load Balancing Weights:\n";
        std::cout << "  - Queue Size: " << weights_.queue_size << "\n";
        std::cout << "  - CPU Load: " << weights_.cpu_load << "\n";
        std::cout << "  - Memory Usage: " << weights_.memory_usage << "\n";
        std::cout << "  - Network Latency: " << weights_.network_latency << "\n";
        std::cout << "  - Distance: " << weights_.distance << "\n\n";
        
        std::cout << "--- Network Topology ---\n";
        std::cout << "Connected Servers:\n";
        for (const auto& server : config_.servers) {
            if (server.server_id() != server_id) {
                std::cout << "  - Server " << server.server_id()
                         << " (" << server.address() << ":" << server.port() << ")\n";
            }
        }
        std::cout << "\n";
        
        std::cout << "--- Initial Metrics ---\n";
        updateMetrics();
        std::cout << "  CPU Load: " << current_metrics_.cpu_utilization() * 100 << "%\n";
        std::cout << "  Memory Usage: " << current_metrics_.memory_usage() * 100 << "%\n";
        std::cout << "  Queue Size: " << current_metrics_.message_queue_length() << "\n";
        std::cout << "  Network Latency: " << current_metrics_.avg_network_latency() << "ms\n\n";
        
        std::cout << "=== Server Ready for Replication ===\n";
        std::cout << "Listening on " << config_.address << ":" << config_.port << "\n\n";
    }

    void updateMetrics() {
        cpu_load = getCpuLoad();
        queue_size = local_queue.size();
        network_latency = measureAverageLatency();
        current_metrics_.set_cpu_utilization(cpu_load);
        current_metrics_.set_message_queue_length(queue_size);
        current_metrics_.set_memory_usage(getMemoryUsage());
        current_metrics_.set_avg_network_latency(network_latency);
        current_metrics_.set_max_queue_length(max_queue_size);
        
        // Log metrics periodically
        static auto last_log = std::chrono::system_clock::now();
        auto now = std::chrono::system_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 30) {
            std::cout << "\n--- Performance Metrics Update ---\n";
            std::cout << "Server: " << server_id << "\n";
            std::cout << "  CPU Load: " << current_metrics_.cpu_utilization() * 100 << "%\n";
            std::cout << "  Memory Usage: " << current_metrics_.memory_usage() * 100 << "%\n";
            std::cout << "  Queue Size: " << current_metrics_.message_queue_length() << "\n";
            std::cout << "  Network Latency: " << current_metrics_.avg_network_latency() << "ms\n";
            std::cout << "  Records Processed: " << metrics_collector_.getTotalRecordsProcessed() << "\n";
            std::cout << "  Work Steals: " << metrics_collector_.getWorkStealCount(server_id) << "\n";
            std::cout << "  Records Forwarded: " << metrics_collector_.getRecordsForwardedCount(server_id) << "\n";
            last_log = now;
        }
    }

    void processAndReplicate(const CollisionData& data) {
        // Add to local queue if we have space
        if (queue_size < max_queue_size) {
            local_queue.push_back(data);
            queue_size++;
            metrics_collector_.recordRecordProcessed();
            
            // Replicate to other servers (write quorum)
            replicateData(data);
        } else {
            // Queue is full, try to steal work
            stealWorkFromOtherServers();
        }
    }

    // Add this method to the GenericServer class
    ServerMetrics getCurrentMetrics() {
        ServerMetrics metrics;
        metrics.set_cpu_utilization(cpu_load);
        metrics.set_message_queue_length(queue_size);
        metrics.set_memory_usage(getMemoryUsage());
        metrics.set_avg_network_latency(network_latency);
        return metrics;
    }

public:
    GenericServer(const std::string& config_path) 
        : server_id("server_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())),
          is_entry_point(false),
          spatialAnalysis(10, 2),  // 10 injuries or 2 deaths to mark area high-risk
          config_(loadConfig(config_path)),
          replication_manager_(config_, server_id),
          metrics_collector_(server_id) {
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
        server_address = config["address"];
        server_port = config["port"];
        is_entry_point = config["is_entry_point"];
        
        std::cout << "Configuring server " << server_id 
                  << " as " << (is_entry_point ? "entry point" : "regular server") 
                  << std::endl;
        
        // Initialize total dataset size from config if specified
        if (config.contains("total_dataset_size")) {
            total_dataset_size = config["total_dataset_size"];
            std::cout << "Using dataset size from config: " 
                      << total_dataset_size << " records" << std::endl;
        }
        
        // Load configuration from JSON file
        std::ifstream config_file_old(config_path);
        if (!config_file_old.is_open()) {
            std::cerr << "Failed to open config file: " << config_path << std::endl;
            exit(1);
        }
        
        json config_old;
        config_file_old >> config_old;
        
        // Parse network configuration
        for (const auto& node : config_old["network"]) {
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
    
    // Handle incoming collision data from client
    Status StreamCollisions(ServerContext* context,
                          grpc::ServerReader<CollisionData>* reader,
                          Empty* response) {
        CollisionData collision;
        int count = 0;
        
        // Read streaming data from client
        while (reader->Read(&collision)) {
            count++;
            total_records_seen++;
            
            // Log progress
            if (count % 100 == 0) {
                std::cout << "Received " << count << " records" << std::endl;
            }
            
            // Process the data locally and replicate
            processAndReplicate(collision);
            
            // Update metrics
            updateMetrics();
            
            // Check if we should steal work from other servers
            if (shouldStealWork()) {
                stealWorkFromOtherServers();
            }
        }
        
        return Status::OK;
    }
    
    // Handle work stealing requests
    Status StealWork(ServerContext* context,
                    const StealRequest* request,
                    StealResponse* response) override {
        // Check if we have enough work to share
        if (local_queue.size() < min_queue_size) {
            return Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                        "Not enough work to share");
        }
        
        // Calculate how many items to share based on our load
        int items_to_share = calculateItemsToShare();
        
        // Share the items
        for (int i = 0; i < items_to_share && !local_queue.empty(); i++) {
            auto* message = response->add_stolen_messages();
            *message = local_queue.back();
            local_queue.pop_back();
        }
        
        // Update metrics
        metrics_collector_.recordWorkSteal(request->requester_id(), server_id);
        return Status::OK;
    }
    
    // Handle metrics updates
    Status UpdateMetrics(ServerContext* context,
                        const MetricsUpdate* request,
                        MetricsResponse* response) override {
        // Update our metrics
        updateMetrics();
        
        // Return current metrics
        response->set_success(true);
        response->set_message("Metrics updated successfully");
        return Status::OK;
    }
    
    // Handle replica synchronization
    Status SyncReplicas(ServerContext* context,
                       const SyncRequest* request,
                       SyncResponse* response) override {
        // Update hop counts
        hop_counts[request->server_id()] = 1;  // Default hop count
        
        // Calculate and return our rank
        double rank = calculateServerRank();
        response->set_success(true);
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
                         Empty* response) {
        total_dataset_size = info->total_size();
        std::cout << "Received dataset size information: " << total_dataset_size << " records" << std::endl;
        
        // Forward this information to all connected servers
        broadcastDatasetSize();
        
        return Status::OK;
    }
    
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

    bool shouldStealWork() {
        return queue_size < min_queue_size && 
               cpu_load < steal_threshold;
    }
    
    void stealWorkFromOtherServers() {
        // Get server ranks
        updateServerRanks();
        
        // Try to steal from servers with higher ranks
        for (const auto& [server_id, rank] : server_ranks) {
            if (rank > calculateServerRank() && 
                hop_counts[server_id] <= max_steal_distance) {
                
                StealRequest request;
                request.set_requester_id(server_id);
                request.set_requested_count(calculateItemsToShare());
                *request.mutable_requester_metrics() = getCurrentMetrics();
                
                StealResponse response;
                ClientContext context;
                auto status = server_stubs[server_id]->StealWork(&context, request, &response);
            
            if (status.ok()) {
                    for (const auto& message : response.stolen_messages()) {
                        processAndReplicate(message);
                    }
                }
            }
        }
    }
    
    void updateServerRanks() {
        // Request metrics from all servers
        for (const auto& [server_id, stub] : server_stubs) {
            MetricsUpdate request;
            request.set_server_id(server_id);
            auto* metrics = request.mutable_metrics();
            metrics->set_cpu_utilization(cpu_load);
            metrics->set_message_queue_length(queue_size);
            metrics->set_memory_usage(getMemoryUsage());
            metrics->set_avg_network_latency(network_latency);
            
            ClientContext context;
            MetricsResponse response;
            
            auto status = stub->UpdateMetrics(&context, request, &response);
            if (status.ok() && response.success()) {
                // Calculate rank based on metrics
                double rank = calculateServerRank();
                server_ranks[server_id] = rank;
            }
        }
    }
    
    double calculateServerRank() {
        double score = 
            weights_.queue_size * normalizeQueueSize(queue_size) +
            weights_.cpu_load * cpu_load +
            weights_.memory_usage * getMemoryUsage() +
            weights_.network_latency * normalizeLatency(network_latency) +
            weights_.distance * normalizeDistance(calculateDistance(server_id));
        return score;
    }
    
    int calculateItemsToShare() {
        // Share more items if we have high load
        return std::min(10, static_cast<int>(queue_size * 0.1));
    }
    
    int calculateItemsToSteal() {
        // Steal more items if we have low load
        return std::min(10, static_cast<int>((min_queue_size - queue_size) * 0.5));
    }

    void replicateData(const CollisionData& data) {
        // Get server ranks
        updateServerRanks();
        
        // Sort servers by rank
        std::vector<std::pair<std::string, double>> sorted_servers;
        for (const auto& [server_id, rank] : server_ranks) {
            if (server_id != this->server_id) {
                sorted_servers.emplace_back(server_id, rank);
            }
        }
        std::sort(sorted_servers.begin(), sorted_servers.end(),
                 [](const auto& a, const auto& b) { return a.second > b.second; });
        
        // Replicate to W-1 servers (since we already have one copy)
        int replicas_created = 0;
        for (const auto& [server_id, rank] : sorted_servers) {
            if (replicas_created >= W-1) break;
            
            // Check if server is within max_steal_distance
            if (hop_counts[server_id] > max_steal_distance) continue;
            
            // Send data to server
            StealRequest request;
            request.set_requester_id(server_id);
            request.set_requested_count(1);
            *request.mutable_requester_metrics() = getCurrentMetrics();
            
            StealResponse response;
            ClientContext context;
            auto status = server_stubs[server_id]->StealWork(&context, request, &response);
            
            if (status.ok()) {
                replicas_created++;
            }
        }
    }
};

// Create a wrapper service class
class ServerService : public InterServerService::Service {
public:
    explicit ServerService(GenericServer* server) : server_(server) {}

    Status StreamCollisions(ServerContext* context,
                          grpc::ServerReader<CollisionData>* reader,
                          Empty* response) {
        return server_->StreamCollisions(context, reader, response);
    }

    Status StealWork(ServerContext* context,
                    const StealRequest* request,
                    StealResponse* response) {
        return server_->StealWork(context, request, response);
    }

    Status UpdateMetrics(ServerContext* context,
                        const MetricsUpdate* request,
                        MetricsResponse* response) {
        return server_->UpdateMetrics(context, request, response);
    }

    Status SyncReplicas(ServerContext* context,
                       const SyncRequest* request,
                       SyncResponse* response) {
        return server_->SyncReplicas(context, request, response);
    }

private:
    GenericServer* server_;
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
    
    // Create the server service
    ServerService service(&server);
    
    // Set up gRPC server
    ServerBuilder builder;
    builder.AddListeningPort(server.getServerAddress(), grpc::InsecureServerCredentials());

    // Register the server service
    builder.RegisterService(&service);
    
    // Start the server
    std::unique_ptr<Server> grpc_server(builder.BuildAndStart());
    std::cout << "Server listening on " << server.getServerAddress() << std::endl;
    
    // Wait for server to finish
    grpc_server->Wait();
    
    return 0;
}