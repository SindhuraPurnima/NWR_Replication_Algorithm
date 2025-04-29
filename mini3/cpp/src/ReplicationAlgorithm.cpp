#include "ReplicationAlgorithm.h"
#include <algorithm>
#include <cmath>

ReplicationAlgorithm::ReplicationAlgorithm(const std::string& server_id, const Config& config)
    : server_id_(server_id), config_(config) {}

double ReplicationAlgorithm::calculateRank(const ServerMetrics& metrics) {
    // Implement the ranking equation: Rank = c0x0 + c1x1 + c2x2 ...
    double rank = 
        config_.weights.queue_size * normalizeQueueSize(metrics.queue_size) +
        config_.weights.cpu_load * metrics.cpu_load +
        config_.weights.network_latency * normalizeLatency(metrics.network_latency) +
        config_.weights.hop_distance * (1.0 / (metrics.hop_distance + 1)) +
        config_.weights.steal_history * calculateStealPenalty(metrics.last_steal_time);
    
    return rank;
}

bool ReplicationAlgorithm::shouldStealWork(const ServerMetrics& metrics) {
    const double MIN_STEAL_INTERVAL = 5.0; // seconds
    auto now = std::chrono::system_clock::now();
    auto time_since_last_steal = 
        std::chrono::duration_cast<std::chrono::seconds>(
            now - metrics.last_steal_time).count();

    return metrics.queue_size < config_.stealing_threshold &&
           metrics.cpu_load < 0.7 &&
           time_since_last_steal > MIN_STEAL_INTERVAL;
}

std::string ReplicationAlgorithm::findBestServer(
    const std::map<std::string, ServerMetrics>& server_metrics) {
    
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

// Helper functions
double ReplicationAlgorithm::normalizeQueueSize(int size) {
    const int MAX_QUEUE_SIZE = 1000; // Can be configured
    return 1.0 - std::min(1.0, static_cast<double>(size) / MAX_QUEUE_SIZE);
}

double ReplicationAlgorithm::normalizeLatency(double latency) {
    const double MAX_LATENCY = 1000.0; // 1 second
    return 1.0 - std::min(1.0, latency / MAX_LATENCY);
}

double ReplicationAlgorithm::calculateStealPenalty(
    const std::chrono::system_clock::time_point& last_steal) {
    
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_steal).count();
    
    // Penalty decreases over time
    return std::exp(-duration / 30.0); // 30-second decay
}
