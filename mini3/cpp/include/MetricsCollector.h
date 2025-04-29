#pragma once
#include <chrono>
#include "ServerMetrics.h"

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
