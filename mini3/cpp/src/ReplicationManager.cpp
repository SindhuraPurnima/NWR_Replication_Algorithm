#include "ReplicationManager.h"

ReplicationManager::ReplicationManager() {
    // Initialize weights from config
    weights_.queue_size = 0.4;
    weights_.cpu_load = 0.3;
    weights_.network_latency = 0.3;
    stealingThreshold_ = 0.7;
}

double ReplicationManager::calculateScore(const ServerMetrics& metrics) {
    // Implement ranking equation: Rank = c0x0 + c1x1 + c2x2
    return weights_.queue_size * metrics.queue_size +
           weights_.cpu_load * metrics.cpu_load +
           weights_.network_latency * metrics.network_latency;
}

bool ReplicationManager::shouldStealWork(const std::string& serverId) {
    // Implement work stealing logic
    return metrics_[serverId].queue_size > stealingThreshold_;
}

bool ReplicationManager::handleMessage(const Message& msg) {
    // 1. Calculate scores for all servers
    std::map<std::string, double> scores;
    for (const auto& [server_id, stub] : server_stubs_) {
        MetricsUpdate metrics;
        auto status = stub->UpdateMetrics(&metrics);
        if (status.ok()) {
            scores[server_id] = calculateScore(metrics);
        }
    }

    // 2. Find best server for message
    std::string best_server = findBestServer(scores);
    
    // 3. Route message or handle locally
    if (best_server == server_id_) {
        message_queue_.push_back(msg);
        return true;
    } else {
        return forwardMessage(best_server, msg);
    }
}

void ReplicationManager::balanceLoad() {
    // Check if we need to steal work
    if (current_metrics_.queue_size < config_.stealing_threshold) {
        // Find overloaded neighbors
        for (const auto& [server_id, stub] : server_stubs_) {
            MetricsUpdate metrics;
            auto status = stub->UpdateMetrics(&metrics);
            if (status.ok() && metrics.queue_size > config_.stealing_threshold) {
                // Steal work
                StealRequest request;
                request.set_source_server(server_id_);
                request.set_requested_items(5); // Can be configured
                StealResponse response;
                auto steal_status = stub->StealWork(&request, &response);
                if (steal_status.ok()) {
                    // Handle stolen messages
                    for (const auto& msg : response.stolen_messages()) {
                        message_queue_.push_back(msg);
                    }
                }
            }
        }
    }
}
