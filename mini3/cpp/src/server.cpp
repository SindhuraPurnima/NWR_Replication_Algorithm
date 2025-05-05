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

// Utility function to normalize queue size to a 0-1 scale
double normalizeQueueSize(int size) {
    const int MAX_QUEUE_SIZE = 1000;
    return static_cast<double>(size) / MAX_QUEUE_SIZE;
}

// Utility function to normalize network latency to a 0-1 scale
double normalizeLatency(double latency) {
    const double MAX_LATENCY = 1000.0;
    return std::min(1.0, latency / MAX_LATENCY);
}

// Utility function to normalize distance to a 0-1 scale
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
    std::cout << "DEBUG: Entering loadConfig with path: " << config_path << std::endl;
    ReplicationManager::Config config;
    
    // Set default values in case the config file doesn't have all needed fields
    config.server_id = "server1";
    config.address = "localhost";
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

    std::cout << "DEBUG: Default configuration set" << std::endl;
    
    // Load the actual config file
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        std::cerr << "Using default configuration" << std::endl;
        return config;
    }
    
    try {
        // Parse the JSON
        std::cout << "DEBUG: Reading JSON config file" << std::endl;
        json json_config;
        config_file >> json_config;
        std::cout << "DEBUG: JSON parsed successfully" << std::endl;
        
        // Set basic server info if available
        if (json_config.contains("server_id")) {
            config.server_id = json_config["server_id"];
            std::cout << "DEBUG: Set server_id = " << config.server_id << std::endl;
        }
        
        if (json_config.contains("address")) {
            config.address = json_config["address"];
            std::cout << "DEBUG: Set address = " << config.address << std::endl;
        }
        
        if (json_config.contains("port")) {
            config.port = json_config["port"];
            std::cout << "DEBUG: Set port = " << config.port << std::endl;
        }
        
        // Parse work stealing parameters if available
        if (json_config.contains("work_stealing")) {
            std::cout << "DEBUG: Found work_stealing section" << std::endl;
            auto& ws = json_config["work_stealing"];
            if (ws.contains("stealing_threshold")) {
                config.stealing_threshold = ws["stealing_threshold"].get<double>();
                std::cout << "DEBUG: Set stealing_threshold = " << config.stealing_threshold << std::endl;
            }
            if (ws.contains("enabled") && !ws["enabled"].get<bool>()) {
                // If stealing is disabled, set a high threshold so it doesn't happen
                config.stealing_threshold = 999.0;
                std::cout << "DEBUG: Work stealing disabled, set threshold to 999.0" << std::endl;
            }
        }
        
        // Parse metrics configuration if available
        if (json_config.contains("metrics")) {
            std::cout << "DEBUG: Found metrics section" << std::endl;
            auto& metrics = json_config["metrics"];
            if (metrics.contains("collection_interval_ms")) {
                config.metrics_interval_ms = metrics["collection_interval_ms"].get<int>();
                std::cout << "DEBUG: Set metrics_interval_ms = " << config.metrics_interval_ms << std::endl;
            }
            if (metrics.contains("max_queue_size")) {
                config.max_queue_size = metrics["max_queue_size"].get<int>();
                std::cout << "DEBUG: Set max_queue_size = " << config.max_queue_size << std::endl;
            }
        }
        
        // CRITICAL FIX: Parse the servers array and convert to ServerInfo objects
        if (json_config.contains("servers") && json_config["servers"].is_array()) {
            std::cout << "DEBUG: Found servers array with " << json_config["servers"].size() << " entries" << std::endl;
            for (const auto& server_json : json_config["servers"]) {
                std::cout << "DEBUG: Processing server entry" << std::endl;
                if (server_json.contains("id") && server_json.contains("address") && server_json.contains("port")) {
                    ServerInfo server_info;
                    server_info.set_server_id(server_json["id"].get<std::string>());
                    server_info.set_address(server_json["address"].get<std::string>());
                    server_info.set_port(server_json["port"].get<int>());
                    
                    // Create default metrics for the server
                    ServerMetrics* metrics = server_info.mutable_metrics();
                    metrics->set_cpu_utilization(0.5);
                    metrics->set_memory_usage(0.4);
                    metrics->set_avg_network_latency(50.0);
                    metrics->set_message_queue_length(0);
                    metrics->set_max_queue_length(config.max_queue_size);
                    
                    // Add to config
                    config.servers.push_back(server_info);
                    
                    std::cout << "DEBUG: Added server: " << server_info.server_id() 
                              << " at " << server_info.address() << ":" << server_info.port() << std::endl;
                }
            }
        } else {
            std::cerr << "Warning: No valid servers array found in config file. "
                      << "Server will run in standalone mode." << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config file: " << e.what() << std::endl;
        std::cerr << "Using default configuration" << std::endl;
    }
    
    std::cout << "DEBUG: loadConfig complete, returning config" << std::endl;
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
    std::map<std::string, std::unique_ptr<InterServerService::Stub>> server_stubs_;
    
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
    std::map<std::string, int> hop_counts_;       // For distance tracking
    
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
    
    // Calculate network distance between servers
    double calculateDistance(const std::string& target_server_id) const {
        if (network_nodes.find(target_server_id) != network_nodes.end()) {
            return hop_counts_.at(target_server_id);
        }
        return 10; // Default high distance for unknown servers
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
            server_stubs_[server_id] = InterServerService::NewStub(channel);
            std::cout << "Created channel to server " << server_id << " at " << target_address << std::endl;
        }
    }
    
    // Forward data to connected servers via gRPC
    void forwardDataToServer(const std::string& server_id, const CollisionBatch& batch) {
        metrics_collector_.recordRecordForwarded(server_id);
        if (server_stubs_.find(server_id) == server_stubs_.end()) {
            initServerStub(server_id);
        }
        
        ClientContext context;
        Empty response;
        Status status = server_stubs_[server_id]->ForwardData(&context, batch, &response);
        
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
    
    // Add these helper methods for normalization inside the class
    double normalizeQueueSizeForService(int size) const {
        if (max_queue_size == 0) return 0.0;
        return static_cast<double>(size) / max_queue_size;
    }

    double normalizeLatencyForService(double latency) const {
        // Assuming max acceptable latency is 200ms
        const double max_latency = 200.0;
        return latency / max_latency;
    }

    double normalizeDistanceForService(double distance) const {
        // Assuming max acceptable distance is 10 hops
        const double max_distance = 10.0;
        return distance / max_distance;
    }
    
    // Update the calculateRank function to use weights_
    double calculateRank(const ServerMetrics& metrics) const {
        double score = 
            weights_.queue_size * normalizeQueueSizeForService(metrics.message_queue_length()) +
            weights_.cpu_load * metrics.cpu_utilization() +
            weights_.memory_usage * metrics.memory_usage() +
            weights_.network_latency * normalizeLatencyForService(metrics.avg_network_latency()) +
            weights_.distance * normalizeDistanceForService(calculateDistance(server_id));
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

    // Update analyzeNetworkTopology to read from config and initialize connections
    void analyzeNetworkTopology() {
        std::cout << "\n=== Server " << server_id << " Initialization Report ===\n";
        std::cout << "Address: " << server_address << ":" << server_port << "\n\n";
        
        // Get the actual configured threshold from config file
        double threshold = 0.4; // Default fallback value
        try {
            std::ifstream config_file("config_" + server_id + ".json");
            if (config_file.is_open()) {
                json config;
                config_file >> config;
                if (config.contains("work_stealing") && config["work_stealing"].contains("stealing_threshold")) {
                    threshold = config["work_stealing"]["stealing_threshold"];
                }
                
                // Also initialize server connections from the config
                if (config.contains("servers")) {
                    for (const auto& server_config : config["servers"]) {
                        std::string id = server_config["id"];
                        if (id != server_id) {  // Don't connect to self
                            std::string address = server_config["address"];
                            int port = server_config["port"];
                            
                            // Create stub for this server
                            std::string server_address = address + ":" + std::to_string(port);
                            auto channel = grpc::CreateChannel(server_address, 
                                                              grpc::InsecureChannelCredentials());
                            server_stubs_[id] = InterServerService::NewStub(channel);
                            
                            // Store hop count if available (default to 1)
                            int hops = 1;
                            if (config.contains("network_simulation") && 
                                config["network_simulation"].contains("topology") &&
                                config["network_simulation"]["topology"].contains(id)) {
                                hops = config["network_simulation"]["topology"][id];
                            }
                            hop_counts_[id] = hops;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error reading config for topology analysis: " << e.what() << std::endl;
        }
        
        std::cout << "--- Replication Configuration ---\n";
        std::cout << "Min Replicas: 3\n";
        std::cout << "Work Stealing Threshold: " << threshold << "\n";
        steal_threshold = threshold; // Update the threshold with the correct value
        
        std::cout << "--- Work Stealing Weights ---\n";
        std::cout << "  - Queue Size: " << weights_.queue_size << "\n";
        std::cout << "  - CPU Load: " << weights_.cpu_load << "\n";
        std::cout << "  - Memory Usage: " << weights_.memory_usage << "\n";
        std::cout << "  - Network Latency: " << weights_.network_latency << "\n";
        std::cout << "  - Distance: " << weights_.distance << "\n\n";
        
        // Check if we successfully established connections
        if (!server_stubs_.empty()) {
            std::cout << "  Connected to servers:\n";
            for (const auto& [id, stub] : server_stubs_) {
                std::cout << "  - Server " << id
                         << " (hops: " << hop_counts_[id] << ")\n";
            }
        } else {
            std::cout << "  No servers configured\n";
        }
        std::cout << "\n";
        
        std::cout << "--- Initial Metrics ---\n";
        std::cout << "  CPU Load: " << (current_metrics_.cpu_utilization() * 100) << "%\n";
        std::cout << "  Memory Usage: " << (current_metrics_.memory_usage() * 100) << "%\n";
        std::cout << "  Queue Size: " << current_metrics_.message_queue_length() << "\n";
        std::cout << "  Network Latency: " << current_metrics_.avg_network_latency() << "ms\n\n";
        
        std::cout << "=== Server Ready for Replication ===\n";
        std::cout << "Listening on " << server_address << ":" << server_port << "\n\n";
    }

public:
    GenericServer(const std::string& config_path) 
        : server_id("server1"), // Default ID
          server_address("localhost"), // Default address
          server_port(50051), // Default port
          is_entry_point(false),
          spatialAnalysis(10, 2),  // 10 injuries or 2 deaths to mark area high-risk
          metrics_collector_(server_id),
          config_(), // Initialize config with default values
          replication_manager_(initConfig(config_path), "server1") { // Initialize with temp config
        
        std::cout << "DEBUG: GenericServer constructor started" << std::endl;
        
        try {
            // Initialize weights
            weights_.queue_size = 0.3;
            weights_.cpu_load = 0.2;
            weights_.memory_usage = 0.2;
            weights_.network_latency = 0.2;
            weights_.distance = 0.1;
        
        // Load configuration from JSON file
            std::cout << "DEBUG: Loading configuration from " << config_path << std::endl;
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            std::cerr << "Failed to open config file: " << config_path << std::endl;
                std::cout << "DEBUG: Using default configuration" << std::endl;
            } else {
                std::cout << "DEBUG: Parsing JSON config" << std::endl;
        json config;
        config_file >> config;
        
                // Parse basic server configuration
                if (config.contains("server_id")) {
                    server_id = config["server_id"];
                    std::cout << "DEBUG: Set server_id = " << server_id << std::endl;
                }
                
                if (config.contains("address")) {
        server_address = config["address"];
                    std::cout << "DEBUG: Set address = " << server_address << std::endl;
                }
                
                if (config.contains("port")) {
        server_port = config["port"];
                    std::cout << "DEBUG: Set port = " << server_port << std::endl;
                }
                
                if (config.contains("is_entry_point")) {
        is_entry_point = config["is_entry_point"];
                    std::cout << "DEBUG: Set is_entry_point = " << is_entry_point << std::endl;
                }
            }
        
            std::cout << "DEBUG: Basic configuration parsed" << std::endl;
        std::cout << "Configuring server " << server_id 
                  << " as " << (is_entry_point ? "entry point" : "regular server") 
                  << std::endl;
        
            // Initialize metrics
            std::cout << "DEBUG: Initializing metrics" << std::endl;
            ServerMetrics metrics;
            
            // Try to read initial metrics from config
            if (config_file.is_open()) {
                try {
                    json config;
                    config_file.seekg(0); // Rewind the file to the beginning
                    config_file >> config;
                    
                    if (config.contains("metrics") && config["metrics"].contains("initial_values")) {
                        auto& initial = config["metrics"]["initial_values"];
                        if (initial.contains("cpu_utilization")) {
                            metrics.set_cpu_utilization(initial["cpu_utilization"]);
                            std::cout << "DEBUG: Set initial CPU utilization = " << metrics.cpu_utilization() << std::endl;
                        } else {
                            metrics.set_cpu_utilization(0.5);
                        }
                        
                        if (initial.contains("memory_usage")) {
                            metrics.set_memory_usage(initial["memory_usage"]);
                            std::cout << "DEBUG: Set initial memory usage = " << metrics.memory_usage() << std::endl;
                        } else {
                            metrics.set_memory_usage(0.4);
                        }
                        
                        if (initial.contains("message_queue_length")) {
                            metrics.set_message_queue_length(initial["message_queue_length"]);
                            std::cout << "DEBUG: Set initial queue length = " << metrics.message_queue_length() << std::endl;
                        } else {
                            metrics.set_message_queue_length(0);
                        }
                        
                        if (initial.contains("network_latency")) {
                            metrics.set_avg_network_latency(initial["network_latency"]);
                            std::cout << "DEBUG: Set initial network latency = " << metrics.avg_network_latency() << std::endl;
                        } else {
                            metrics.set_avg_network_latency(50.0);
                        }
                    } else {
                        // Use default values
                        metrics.set_cpu_utilization(0.5);
                        metrics.set_memory_usage(0.4);
                        metrics.set_message_queue_length(0);
                        metrics.set_avg_network_latency(50.0);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing metrics from config: " << e.what() << std::endl;
                    // Use default values
                    metrics.set_cpu_utilization(0.5);
                    metrics.set_memory_usage(0.4);
                    metrics.set_message_queue_length(0);
                    metrics.set_avg_network_latency(50.0);
                }
            } else {
                // Use default values
                metrics.set_cpu_utilization(0.5);
                metrics.set_memory_usage(0.4);
                metrics.set_message_queue_length(0);
                metrics.set_avg_network_latency(50.0);
            }
            
            metrics.set_max_queue_length(1000);
            current_metrics_ = metrics;
            
            // Register ourselves in the metrics collector
            ServerInfo self_info;
            self_info.set_server_id(server_id);
            self_info.set_address(server_address);
            self_info.set_port(server_port);
            *self_info.mutable_metrics() = metrics;
            metrics_collector_.updateServerMetrics(self_info);
            
            // Simple network topology analysis
            std::cout << "DEBUG: Calling analyzeNetworkTopology" << std::endl;
            analyzeNetworkTopology();
            
            // Set total node count to 1 for standalone mode
            total_node_count = 1;
            std::cout << "DEBUG: GenericServer constructor completed" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "ERROR in GenericServer constructor: " << e.what() << std::endl;
            throw; // Rethrow to be caught in main
        }
        catch (...) {
            std::cerr << "UNKNOWN ERROR in GenericServer constructor" << std::endl;
            throw; // Rethrow to be caught in main
        }
    }
    
    // Add accessor methods for ServerService
    const std::string& getServerId() const {
        return server_id;
    }
    
    double calculateServerRankForService() const {
        return calculateRank(current_metrics_);
    }
    
    double calculateRankForService(const ServerMetrics& metrics) const {
        return calculateRank(metrics);
    }
    
    const MetricsCollector& getMetricsCollector() const {
        return metrics_collector_;
    }
    
    void processCollisionLocally(const CollisionData& data) {
        processAndReplicate(data);
    }
    
    // Add method to forward a collision to another server
    void forwardCollisionToServer(const std::string& server_address, const CollisionData& collision) {
        // Create a new client stub to the target server
        auto channel = grpc::CreateChannel(
            server_address, grpc::InsecureChannelCredentials());
        auto stub = InterServerService::NewStub(channel);
        
        // Create a batch with the collision
        CollisionBatch batch;
        *batch.add_collisions() = collision;
        
        // Forward the data
        grpc::ClientContext context;
        Empty response;
        Status status = stub->ForwardData(&context, batch, &response);
        
        if (!status.ok()) {
            std::cerr << "Failed to forward data to " << server_address 
                    << ": " << status.error_message() << std::endl;
        }
    }
    
    // Add method to print all server metrics
    void printAllServerMetrics() {
        std::cout << "\n=== SERVER METRICS REPORT ===\n";
        std::cout << "Current server: " << server_id << ", rank: " << calculateServerRankForService() << "\n\n";
        
        std::cout << "All known servers:\n";
        for (const auto& server_info : metrics_collector_.getAllServerMetrics()) {
            double rank = calculateRankForService(server_info.metrics());
            std::cout << "  Server ID: " << server_info.server_id() 
                      << ", Address: " << server_info.address() << ":" << server_info.port()
                      << ", Rank: " << rank << "\n";
            std::cout << "    CPU: " << server_info.metrics().cpu_utilization() 
                      << ", Memory: " << server_info.metrics().memory_usage()
                      << ", Queue: " << server_info.metrics().message_queue_length() 
                      << ", Latency: " << server_info.metrics().avg_network_latency() << "ms\n";
        }
        std::cout << std::endl;
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
        
        // Set up periodic health check timer
        auto last_health_check = std::chrono::system_clock::now();
        
        // Read streaming data from client
        while (reader->Read(&collision)) {
            count++;
            total_records_seen++;
            
            // Log progress and periodically print metrics
            if (count % 100 == 0) {
                std::cout << "Received " << count << " records" << std::endl;
                printAllServerMetrics();
            }
            
            // Process the data locally and replicate
            processAndReplicate(collision);
            
            // Update metrics
            updateMetrics();
            
            // Check if we should steal work from other servers
            if (shouldStealWork()) {
                stealWorkFromOtherServers();
            }
            
            // Periodically check server health (every 10 seconds)
            auto now = std::chrono::system_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_health_check).count() >= 10) {
                checkServerHealth();
                last_health_check = now;
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
                        MetricsResponse* response) {
        // Update our metrics first
        updateMetrics();
        
        // Set response to success
        response->set_success(true);
        response->set_message("Metrics updated successfully");
        
        // Record the metrics from the requesting server
        if (request->has_metrics()) {
            ServerInfo info;
            info.set_server_id(request->server_id());
            // Use the local server address since we don't have it in the request
            info.set_address("localhost");
            
            // Determine port based on server_id
            int port = 50051; // default
            if (request->server_id() == "server2") port = 50052;
            if (request->server_id() == "server3") port = 50053;
            if (request->server_id() == "server4") port = 50054;
            if (request->server_id() == "server5") port = 50055;
            info.set_port(port);
            
            // Copy metrics
            *info.mutable_metrics() = request->metrics();
            
            // Update metrics in our collector
            metrics_collector_.updateServerMetrics(info);
        }
        
        return Status::OK;
    }
    
    // Handle replica synchronization
    Status SyncReplicas(ServerContext* context,
                       const SyncRequest* request,
                       SyncResponse* response) override {
        // Update hop counts
        hop_counts_[request->server_id()] = 1;  // Default hop count
        
        // Calculate and return our rank
        double rank = calculateServerRank();
        response->set_success(true);
        return Status::OK;
    }
    
    // Get server address (IP:port)
    std::string getServerAddress() const {
        return server_address + ":" + std::to_string(server_port);
    }
    
    int getServerPort() const {
        return server_port;
    }
    
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
            
            if (server_stubs_.find(server_id) == server_stubs_.end()) {
                initServerStub(server_id);
            }
            
            ClientContext new_context;
            Empty new_response;
            Status status = server_stubs_[server_id]->SetTotalDatasetSize(&new_context, *info, &new_response);
            
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

    // Add a method to forward dataset size to all connected servers
    void broadcastDatasetSize() {
        // Only entry point should broadcast the total size
        if (!is_entry_point) return;
        
        // Create RPC message
        DatasetInfo info;
        info.set_total_size(total_dataset_size);
        
        // Send to each connected server
        for (const std::string& server_id : connections) {
            if (server_stubs_.find(server_id) == server_stubs_.end()) {
                initServerStub(server_id);
            }
            
            ClientContext context;
            Empty response;
            
            // We need to add this RPC to the InterServerService too
            Status status = server_stubs_[server_id]->SetTotalDatasetSize(&context, info, &response);
            
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
                hop_counts_[server_id] <= max_steal_distance) {
                
                StealRequest request;
                request.set_requester_id(server_id);
                request.set_requested_count(calculateItemsToShare());
                *request.mutable_requester_metrics() = getCurrentMetrics();
                
                StealResponse response;
                ClientContext context;
                auto status = server_stubs_[server_id]->StealWork(&context, request, &response);
            
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
        for (const auto& [server_id, stub] : server_stubs_) {
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
            weights_.queue_size * normalizeQueueSizeForService(queue_size) +
            weights_.cpu_load * cpu_load +
            weights_.memory_usage * getMemoryUsage() +
            weights_.network_latency * normalizeLatencyForService(network_latency) +
            weights_.distance * normalizeDistanceForService(calculateDistance(server_id));
        return score;
    }
    
    int calculateItemsToShare() {
        // Share more items if we have high load
        return std::min(10, static_cast<int>(queue_size * 0.1));
    }

    void replicateData(const CollisionData& data) {
        // Get server ranks
        updateServerRanks();
        
        if (server_stubs_.empty()) {
            std::cout << "No other servers are available for replication." << std::endl;
            return;
        }
        
        // Create a batch with this collision
        CollisionBatch batch;
        *batch.add_collisions() = data;
        
        // Sort servers by rank (lowest load first)
        std::vector<std::pair<std::string, double>> sorted_servers;
        for (const auto& [server_id, rank] : server_ranks) {
            if (server_id != this->server_id) {
                sorted_servers.emplace_back(server_id, rank);
            }
        }
        std::sort(sorted_servers.begin(), sorted_servers.end(),
                 [](const auto& a, const auto& b) { return a.second < b.second; });
        
        // Replicate to N-1 servers (since we already have one copy)
        int replicas_created = 0;
        std::unordered_set<std::string> already_replicated_collisions;
        static int next_id = 0;  // Static counter that persists between calls
        std::string collision_id = "collision_" + std::to_string(next_id++);
        for (const auto& [target_server_id, rank] : sorted_servers) {
            if (replicas_created >= N-1) break;
            
            // Check if server is within max_steal_distance
            if (hop_counts_.find(target_server_id) == hop_counts_.end() || 
                hop_counts_[target_server_id] > max_steal_distance) continue;
            
            // Send data to server
            std::cout << "WORK STEALING: Redistributing collision " 
                     << collision_id << " to Server" << target_server_id 
                     << " (rank: " << rank << ") based on load balancing." << std::endl;
            
            // Forward data to the target server
            forwardDataToServer(target_server_id, batch);
            replicas_created++;
            
            // Add collision ID to set of already replicated collisions
            if (already_replicated_collisions.find(collision_id) == already_replicated_collisions.end()) {
                // Replicate and log
                already_replicated_collisions.insert(collision_id);
            }
        }
        
        if (replicas_created == 0) {
            std::cout << "WARNING: Could not replicate data " << collision_id 
                     << " to any other servers." << std::endl;
        } else {
            std::cout << "Successfully replicated data " << collision_id 
                     << " to " << replicas_created << " servers." << std::endl;
        }
    }

    // Add method to check if we should redistribute work based on load
    bool shouldRedistributeWork() {
        // Check if this server is heavily loaded (CPU or queue)
        double serverRank = calculateServerRank();
        
        // If our load is above 0.6, we should redistribute
        return serverRank > 0.6;
    }
    
    // Add method to check server health and handle recovery
    void checkServerHealth() {
        for (const auto& server_info : metrics_collector_.getAllServerMetrics()) {
            // Skip checking ourselves
            if (server_info.server_id() == server_id) continue;
            
            std::string target_address = server_info.address() + ":" + 
                                       std::to_string(server_info.port());
            
            // Create a channel to the server
            auto channel = grpc::CreateChannel(
                target_address, grpc::InsecureChannelCredentials());
            auto stub = InterServerService::NewStub(channel);
            
            // Send health check request with timeout
            ServerInfo request;
            request.set_server_id(server_id);
            ServerInfo response;
            ClientContext context;
            
            // Set a deadline for RPC
            std::chrono::system_clock::time_point deadline = 
                std::chrono::system_clock::now() + std::chrono::milliseconds(2000);
            context.set_deadline(deadline);
            
            Status status = stub->HealthCheck(&context, request, &response);
            
            if (!status.ok()) {
                std::cout << "WARNING: Server " << server_info.server_id() 
                         << " (" << target_address << ") is not responding: " 
                         << status.error_message() << std::endl;
                
                // Handle recovery - redistribute work among remaining servers
                handleServerFailure(server_info.server_id());
            }
        }
    }
    
    // Add method to handle server failure
    void handleServerFailure(const std::string& failed_server_id) {
        std::cout << "RECOVERY: Handling failure of server " << failed_server_id << std::endl;
        
        // Remove failed server from metrics
        metrics_collector_.removeServerMetrics(failed_server_id);
        
        // Update our ranks to account for the changed network
        updateServerRanks();
        
        // If we have replicated data from the failed server, we need to ensure
        // that data still meets the replica count requirement
        replicateFailedServerData(failed_server_id);
        
        std::cout << "RECOVERY: Complete for server " << failed_server_id << std::endl;
    }
    
    // Add method to replicate data from failed server
    void replicateFailedServerData(const std::string& failed_server_id) {
        // In a real system, this would identify data originally from the failed server
        // and ensure it's replicated to maintain the required number of copies
        
        // For this example, we'll just log the intent
        std::cout << "RECOVERY: Would replicate data from failed server " 
                 << failed_server_id << " to maintain replica count." << std::endl;
    }

    // Add method to dynamically update metrics based on workload
    void simulateDynamicMetricsChange() {
        // Increase CPU and memory usage as queue gets fuller
        if (queue_size > 0 && max_queue_size > 0) {
            double queue_percentage = static_cast<double>(queue_size) / max_queue_size;
            
            // CPU load increases with queue size but has some randomness
            double base_cpu = 0.1 + (0.7 * queue_percentage);
            double random_factor = (std::rand() % 10) / 100.0; // 0-0.09 random factor
            cpu_load = base_cpu + random_factor;
            cpu_load = std::min(0.98, cpu_load);  // Cap at 98%
            
            // Memory increases with queue but more slowly
            double memory_usage = getMemoryUsage();
            memory_usage += (0.05 * queue_percentage);
            memory_usage = std::min(0.95, memory_usage);
            
            // Update metrics
            current_metrics_.set_cpu_utilization(cpu_load);
            current_metrics_.set_memory_usage(memory_usage);
            
            // Network latency increases slightly with load
            network_latency = 30.0 + (40.0 * queue_percentage);
            current_metrics_.set_avg_network_latency(network_latency);
        }
    }

    // Add updateMetrics here
    void updateMetrics() {
        // Check if we should use dynamic metrics updates
        bool use_dynamic_update = false;
        try {
            // Parse the config file to check if dynamic_update is enabled
            std::ifstream config_file("config_" + server_id + ".json");
            if (config_file.is_open()) {
                json config;
                config_file >> config;
                if (config.contains("metrics") && config["metrics"].contains("dynamic_update")) {
                    use_dynamic_update = config["metrics"]["dynamic_update"];
                }
                config_file.close();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing config for dynamic update: " << e.what() << std::endl;
        }
        
        if (use_dynamic_update) {
            // Use simulated dynamic metrics based on workload
            simulateDynamicMetricsChange();
        } else {
            // Use standard metrics collection
            cpu_load = getCpuLoad();
            queue_size = local_queue.size();
            network_latency = measureAverageLatency();
            current_metrics_.set_cpu_utilization(cpu_load);
            current_metrics_.set_message_queue_length(queue_size);
            current_metrics_.set_memory_usage(getMemoryUsage());
            current_metrics_.set_avg_network_latency(network_latency);
        }
        
        // Always update queue size
        queue_size = local_queue.size();
        current_metrics_.set_message_queue_length(queue_size);
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
            std::cout << "  Server Rank: " << calculateServerRank() << "\n";
            last_log = now;
        }
    }
    
    // Process data locally and replicate
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

    // Helper function to initialize config and allow for constructor initialization
    static ReplicationManager::Config initConfig(const std::string& config_path) {
        std::cout << "DEBUG: Initializing config in helper function" << std::endl;
        ReplicationManager::Config config;
        config.server_id = "server1";
        config.address = "localhost";
        config.port = 50051;
        config.max_queue_size = 1000;
        config.stealing_threshold = 0.7;
        config.stealing_batch_size = 10;
        config.metrics_interval_ms = 1000;
        config.stealing_interval_ms = 5000;
        config.min_replicas = 3;
        return config;
    }

    Status HealthCheck(ServerContext* context,
                      const ServerInfo* request,
                      ServerInfo* response) override {
        // Fill in the response with the server's information
        response->set_server_id(server_id);
        response->set_address(server_address);
        response->set_port(server_port);
        
        // Add current metrics
        auto* metrics = response->mutable_metrics();
        metrics->set_cpu_utilization(current_metrics_.cpu_utilization());
        metrics->set_memory_usage(current_metrics_.memory_usage());
        metrics->set_message_queue_length(current_metrics_.message_queue_length());
        metrics->set_max_queue_length(max_queue_size);
        metrics->set_avg_network_latency(current_metrics_.avg_network_latency());
        
        // Update metrics before sending
        updateMetrics();
        
        return Status::OK;
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
    
    Status ForwardData(ServerContext* context,
                      const CollisionBatch* request,
                      Empty* response) override {
        // Process each collision in the batch
        for (const auto& collision : request->collisions()) {
            // Display more information about the collision
            std::cout << "Received collision data: " 
                      << "ID=" << collision.collision_id()
                      << ", Borough=" << collision.borough()
                      << ", Injured=" << collision.number_of_persons_injured()
                      << ", Killed=" << collision.number_of_persons_killed()
                      << std::endl;
            
            // Update metrics - add this to ensure metrics are current
            server_->updateMetrics();
            
            // Calculate current server's rank
            double my_rank = server_->calculateServerRankForService();
            
            // DYNAMIC LOAD BALANCING - check if we should redistribute
            if (server_->shouldRedistributeWork()) {
                // Use collision ID modulo 5 to pick servers instead of binary decision
                int target_server_num = std::hash<std::string>{}(collision.collision_id()) % 5 + 1;
                std::string target_server_id = "server" + std::to_string(target_server_num);
                
                // Skip ourselves
                if (target_server_id != server_->getServerId()) {
                    // Get target address
                    std::string target_server = "";
                    int port = 50051 + (target_server_num - 1); // Direct port calculation
                    target_server = "localhost:" + std::to_string(port);
                    
                    std::cout << "WORK DISTRIBUTION: Sending collision " << collision.collision_id() 
                              << " to server" << target_server_num << " (port: " << port << ")" << std::endl;
                    server_->forwardCollisionToServer(target_server, collision);
                    continue;
                }
            }
            
            // Process locally if not redistributed
            std::cout << "Processing collision " << collision.collision_id() << " locally" << std::endl;
            server_->processCollisionLocally(collision);
            
            // Update metrics again after processing
            server_->updateMetrics();
        }
        return Status::OK;
    }
    
    Status ShareAnalysis(ServerContext* context,
                        const RiskAssessment* request,
                        Empty* response) override {
        // Simple implementation
        return Status::OK;
    }
    
    Status SetTotalDatasetSize(ServerContext* context,
                              const DatasetInfo* request,
                              Empty* response) override {
        return server_->SetTotalDatasetSize(context, request, response);
    }

    Status StealWork(ServerContext* context,
                    const StealRequest* request,
                    StealResponse* response) override {
        return server_->StealWork(context, request, response);
    }

    Status UpdateMetrics(ServerContext* context,
                        const MetricsUpdate* request,
                        MetricsResponse* response) override {
        return server_->UpdateMetrics(context, request, response);
    }

    Status SyncReplicas(ServerContext* context,
                       const SyncRequest* request,
                       SyncResponse* response) override {
        return server_->SyncReplicas(context, request, response);
    }
    
    Status RegisterServer(ServerContext* context,
                         const ServerInfo* request,
                         ServerList* response) override {
        return server_->RegisterServer(context, request, response);
    }

    Status UnregisterServer(ServerContext* context,
                           const ServerInfo* request,
                           ServerList* response) override {
        return server_->UnregisterServer(context, request, response);
    }

    Status HealthCheck(ServerContext* context,
                      const ServerInfo* request,
                      ServerInfo* response) override {
        return server_->HealthCheck(context, request, response);
    }

private:
    GenericServer* server_;
};

int main(int argc, char** argv) {
    std::cout << "DEBUG: Entering main function" << std::endl;
    
    // Check command line arguments for config file
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }
    
    std::string config_path = argv[1];
    std::cout << "DEBUG: Using config file: " << config_path << std::endl;
    
    // Set up server address
    std::string server_address;
    int server_port;
    
    // Try to load config to get address and port
    try {
        std::cout << "DEBUG: Opening config file to read address and port" << std::endl;
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            std::cerr << "Failed to open config file: " << config_path << std::endl;
            return 1;
        }
        
        json config;
        config_file >> config;
        
        if (config.contains("address")) {
            server_address = config["address"];
        } else {
            server_address = "0.0.0.0";
        }
        
        if (config.contains("port")) {
            server_port = config["port"];
        } else {
            server_port = 50051;
        }
        
        std::cout << "DEBUG: Server will listen on " << server_address << ":" << server_port << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing config: " << e.what() << std::endl;
        server_address = "0.0.0.0";
        server_port = 50051;
    }
    
    // Build server address string
    std::string address = server_address + ":" + std::to_string(server_port);
    
    // Initialize GenericServer with configuration
    std::cout << "DEBUG: Creating GenericServer instance" << std::endl;
    try {
        GenericServer server(config_path);
    ServerService service(&server);
    
    // Set up gRPC server
        std::cout << "DEBUG: Setting up gRPC server" << std::endl;
    ServerBuilder builder;
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
        // Start server
        std::cout << "DEBUG: Starting gRPC server" << std::endl;
        std::unique_ptr<Server> server_ptr = builder.BuildAndStart();
        std::cout << "Server listening on " << address << std::endl;
        
        // Keep server running
        server_ptr->Wait();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception in server initialization: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception in server initialization" << std::endl;
        return 1;
    }
    
    return 0;
}