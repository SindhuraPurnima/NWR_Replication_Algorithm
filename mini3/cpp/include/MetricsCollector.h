#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include "proto/mini2.grpc.pb.h"

using namespace mini2;

class MetricsCollector {
public:
    struct SystemMetrics {
        double cpu_load;
        double memory_usage;
        double network_latency;
        std::chrono::system_clock::time_point timestamp;
    };

    explicit MetricsCollector(const std::string& server_id);
    ~MetricsCollector();

    // System metrics collection
    SystemMetrics collectSystemMetrics();
    double getCpuLoad();
    double getMemoryUsage();
    double measureAverageLatency();

    // Server metrics management
    void updateServerMetrics(const ServerInfo& server);
    void removeServerMetrics(const std::string& server_id);
    std::optional<ServerInfo> getServerMetrics(const std::string& server_id) const;
    std::vector<ServerInfo> getAllServerMetrics() const;

    ServerMetrics collectMetrics();
    double measureNetworkLatency(const std::string& target_server);
    void recordWorkSteal(const std::string& source_server, const std::string& target_server);
    void recordRecordProcessed();
    void recordRecordForwarded(const std::string& target_server);
    void generatePerformanceReport(const std::string& filename);
    void logMetrics(const ServerMetrics& metrics);
    bool verifyDataConsistency(const std::vector<std::string>& server_ids);
    void resolveConflicts(const std::vector<std::string>& server_ids);

    int getTotalRecordsProcessed() const { return total_records_processed_; }
    int getWorkStealCount(const std::string& server_id) const { return work_steal_counts_.at(server_id); }
    int getRecordsForwardedCount(const std::string& server_id) const { return records_forwarded_counts_.at(server_id); }

private:
    struct PerformanceMetrics {
        double throughput{0.0};
        double avg_response_time{0.0};
        double load_balance_score{0.0};
        double consistency_score{0.0};
    };

    PerformanceMetrics calculatePerformanceMetrics();
    double calculateLoadBalanceScore();
    double calculateConsistencyScore();

    std::string server_id_;
    std::map<std::string, int> work_steal_counts_;
    std::map<std::string, int> records_forwarded_counts_;
    int total_records_processed_{0};
    std::vector<ServerMetrics> metrics_history_;
    std::chrono::system_clock::time_point last_collection_;
    std::mutex metrics_mutex_;
    std::vector<ServerInfo> server_metrics_;
};
