#pragma once
#include <string>
#include <vector>
#include <map>
#include <grpcpp/grpcpp.h>
#include "proto/replication.grpc.pb.h"

class ReplicationManager {
public:
    ReplicationManager(const std::string& server_id, const json& config);
    
    // Core replication functions
    bool handleMessage(const Message& msg);
    std::vector<Message> handleStealRequest(int requested_items);
    void updateMetrics();
    double calculateScore(const Metrics& metrics);

private:
    std::string server_id_;
    std::vector<Message> message_queue_;
    
    struct ReplicationConfig {
        int min_replicas;
        int max_replicas;
        double stealing_threshold;
        struct {
            double queue_size;
            double cpu_load;
            double network_latency;
        } weights;
    } config_;

    // Current server metrics
    struct Metrics {
        double cpu_load;
        int queue_size;
        double memory_usage;
        double network_latency;
    } current_metrics_;
};
