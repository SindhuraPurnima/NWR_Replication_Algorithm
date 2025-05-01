#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "proto/mini2.grpc.pb.h"

class ReplicationAlgorithm {
public:
    struct ServerMetrics {
        double cpu_load;        // 0.0 to 1.0
        int queue_size;         // Current message queue size
        double memory_usage;    // 0.0 to 1.0
        double network_latency; // in milliseconds
        int hop_distance;       // Number of hops to this server
        std::chrono::system_clock::time_point last_steal_time;
    };

    struct Config {
        double stealing_threshold;
        int min_replicas;
        int max_replicas;
        struct Weights {
            double queue_size;
            double cpu_load;
            double network_latency;
            double hop_distance;
            double steal_history;
        } weights;
    };

    explicit ReplicationAlgorithm(const std::string& server_id, const Config& config);

    // Core functions
    double calculateRank(const ServerMetrics& metrics);
    bool shouldStealWork(const ServerMetrics& metrics);
    std::string findBestServer(const std::map<std::string, ServerMetrics>& server_metrics);

private:
    std::string server_id_;
    Config config_;
    
    // Helper functions
    double normalizeQueueSize(int size);
    double normalizeLatency(double latency);
    double calculateStealPenalty(const std::chrono::system_clock::time_point& last_steal);
};
