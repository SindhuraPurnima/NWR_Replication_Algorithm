#include "ReplicationAlgorithm.h"
#include <algorithm>
#include <cmath>
#include <chrono>

double DefaultReplicationAlgorithm::calculateRank(const ServerMetrics& metrics) {
    double rank = 0.0;
    
    // Message queue length factor (c0x0)
    rank += config_.queue_weight * (1.0 - (metrics.message_queue_length / metrics.max_queue_length));
    
    // CPU utilization factor (c1x1)
    rank += config_.cpu_weight * (1.0 - metrics.cpu_utilization);
    
    // Memory usage factor (c2x2)
    rank += config_.memory_weight * (1.0 - metrics.memory_usage);
    
    // Network latency factor (c3x3)
    rank += config_.network_weight * (1.0 - (metrics.avg_network_latency / metrics.max_acceptable_latency));
    
    // Hop distance factor (c4x4)
    rank += config_.hop_distance_weight * (1.0 / (metrics.hop_distance + 1));
    
    // Steal history factor (c5x5)
    rank += config_.steal_history_weight * calculateStealPenalty(metrics.last_steal_time);
    
    return rank;
}

bool DefaultReplicationAlgorithm::shouldStealWork(const ServerMetrics& metrics) {
    return calculateRank(metrics) < config_.stealing_threshold;
}

bool DefaultReplicationAlgorithm::shouldAcceptWork(const ServerMetrics& metrics) {
    return calculateRank(metrics) > 0.7; // Threshold for accepting work
}

int DefaultReplicationAlgorithm::calculateStealCount(const ServerMetrics& metrics) {
    double rank = calculateRank(metrics);
    int max_steal = metrics.max_queue_length / 4; // Don't steal more than 25% of queue
    return static_cast<int>(max_steal * (1.0 - rank));
}

int DefaultReplicationAlgorithm::calculateMaxStealDistance(const ServerMetrics& metrics) {
    // Base distance on network latency and server load
    double base_distance = 3; // Default max hops
    if (metrics.avg_network_latency > metrics.max_acceptable_latency) {
        base_distance = 1; // Reduce distance if network is slow
    }
    return static_cast<int>(base_distance * (1.0 - metrics.cpu_utilization));
}

std::string DefaultReplicationAlgorithm::findBestServer(const std::map<std::string, ServerMetrics>& server_metrics) {
    double best_rank = -1;
    std::string best_server = server_id_; // Default to self

    for (const auto& [server_id, metrics] : server_metrics) {
        double rank = calculateRank(metrics);
        if (rank > best_rank) {
            best_rank = rank;
            best_server = server_id;
        }
    }

    return best_server;
}

double DefaultReplicationAlgorithm::normalizeQueueSize(int size) {
    const int MAX_QUEUE_SIZE = 1000; // Can be configured
    return 1.0 - std::min(1.0, static_cast<double>(size) / MAX_QUEUE_SIZE);
}

double DefaultReplicationAlgorithm::normalizeLatency(double latency) {
    const double MAX_LATENCY = 1000.0; // 1 second
    return 1.0 - std::min(1.0, latency / MAX_LATENCY);
}

double DefaultReplicationAlgorithm::calculateStealPenalty(const std::chrono::system_clock::time_point& last_steal) {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_steal).count();
    
    // Penalty decreases over time
    return std::exp(-duration / 30.0); // 30-second decay
}
