#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "proto/mini2.grpc.pb.h"

using mini2::Message;
using mini2::StealResponse;
using mini2::MetricsResponse;


class ReplicationManager {
public:
    struct ServerMetrics {
        double cpu_load;        // 0.0 to 1.0
        int queue_size;         // Current queue size
        double memory_usage;    // 0.0 to 1.0
        double network_latency; // in milliseconds
        std::chrono::system_clock::time_point last_update;
    };

    struct Config {
        int min_replicas;
        int max_replicas;
        double stealing_threshold;
        struct {
            double queue_size;
            double cpu_load;
            double memory_usage;
            double network_latency;
        } weights;
    };

    explicit ReplicationManager(const std::string& server_id, const Config& config);
    
    // Core distribution functions
    bool handleMessage(const Message& msg);
    std::vector<Message> handleStealRequest(int requested_count);
    void updateMetrics(const std::string& server_id, const ServerMetrics& metrics);
    double calculateServerRank(const ServerMetrics& metrics);
    std::string selectBestServer();
    bool shouldStealWork();
    ServerMetrics getCurrentMetrics();

private:
    std::string server_id_;
    Config config_;
    std::map<std::string, ServerMetrics> server_metrics_;
    std::vector<Message> message_queue_;
    
    // Helper functions
    double normalizeQueueSize(int size);
    double normalizeLatency(double latency);
    std::vector<std::string> selectReplicaTargets(int count);
};
