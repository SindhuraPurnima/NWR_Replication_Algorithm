#pragma once
#include <chrono>
#include <string>

// Server metrics structure definition
struct ServerMetrics {
    double cpu_load;        // 0.0 to 1.0
    int queue_size;         // Current queue size
    double memory_usage;    // 0.0 to 1.0
    double network_latency; // in milliseconds
    std::chrono::system_clock::time_point last_update;
};

// Metrics collector class definition
class MetricsCollector {
public:
    MetricsCollector(const std::string& server_id);
    
    // Collect current metrics
    ServerMetrics collectMetrics();
    
    // Individual metric collectors
    double getCpuLoad();
    double getMemoryUsage();
    double measureNetworkLatency(const std::string& target_server);
    
private:
    std::string server_id_;
    std::chrono::system_clock::time_point last_collection_;
};
