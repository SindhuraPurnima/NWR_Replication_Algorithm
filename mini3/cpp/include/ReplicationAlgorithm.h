#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <grpcpp/grpcpp.h>
#include "proto/mini2.grpc.pb.h"

struct ServerMetrics {
    double cpu_utilization{0.0};
    double memory_usage{0.0};
    double avg_network_latency{0.0};
    int message_queue_length{0};
    int max_queue_length{1000};
    double avg_message_age{0.0};
    double max_message_age{3600.0};
    double max_acceptable_latency{100.0};
    std::chrono::system_clock::time_point last_update;
    int hop_distance{0};
    std::chrono::system_clock::time_point last_steal_time;
};

struct Config {
    std::string server_id;
    std::string address;
    int port;
    int max_hop_distance{3};          // Maximum number of hops for work stealing
    double stealing_threshold{0.5};    // Threshold for work stealing
    double min_queue_size{10};         // Minimum queue size before stealing
    double max_steal_ratio{0.25};      // Maximum percentage of queue to steal
    int steal_cooldown{30};            // Cooldown period between steals (seconds)
    double network_weight{0.2};        // Weight for network latency in ranking
    double cpu_weight{0.3};            // Weight for CPU utilization in ranking
    double memory_weight{0.2};         // Weight for memory usage in ranking
    double queue_weight{0.3};          // Weight for queue size in ranking
    double hop_distance_weight{0.1};   // Weight for hop distance in ranking
    double steal_history_weight{0.1};  // Weight for steal history in ranking
    std::vector<std::string> servers;  // List of all servers in the network
};

class ReplicationAlgorithm {
public:
    explicit ReplicationAlgorithm(const std::string& server_id, const Config& config) noexcept
        : server_id_(server_id), config_(config) {}
    virtual ~ReplicationAlgorithm() = default;

    // Core replication functions
    virtual double calculateRank(const ServerMetrics& metrics) = 0;
    virtual bool shouldStealWork(const ServerMetrics& metrics) = 0;
    virtual bool shouldAcceptWork(const ServerMetrics& metrics) = 0;
    virtual int calculateStealCount(const ServerMetrics& metrics) = 0;
    virtual int calculateMaxStealDistance(const ServerMetrics& metrics) = 0;
    virtual std::string findBestServer(const std::map<std::string, ServerMetrics>& server_metrics) = 0;

    // Helper functions
    virtual double normalizeQueueSize(int size) = 0;
    virtual double normalizeLatency(double latency) = 0;
    virtual double calculateStealPenalty(const std::chrono::system_clock::time_point& last_steal) = 0;

protected:
    std::string server_id_;
    Config config_;
    std::map<std::string, std::chrono::system_clock::time_point> last_steal_times_;
    std::mutex steal_mutex_;
};

// Concrete implementation of ReplicationAlgorithm
class DefaultReplicationAlgorithm : public ReplicationAlgorithm {
public:
    DefaultReplicationAlgorithm(const std::string& server_id, const Config& config)
        : ReplicationAlgorithm(server_id, config) {}

    double calculateRank(const ServerMetrics& metrics) override;
    bool shouldStealWork(const ServerMetrics& metrics) override;
    bool shouldAcceptWork(const ServerMetrics& metrics) override;
    int calculateStealCount(const ServerMetrics& metrics) override;
    int calculateMaxStealDistance(const ServerMetrics& metrics) override;
    std::string findBestServer(const std::map<std::string, ServerMetrics>& server_metrics) override;

protected:
    double normalizeQueueSize(int size) override;
    double normalizeLatency(double latency) override;
    double calculateStealPenalty(const std::chrono::system_clock::time_point& last_steal) override;
};
