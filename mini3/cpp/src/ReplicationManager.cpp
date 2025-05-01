#include "ReplicationManager.h"
#include <algorithm>
#include <cmath>
#include <chrono>

ReplicationManager::ReplicationManager(const std::string& server_id, const Config& config)
    : server_id_(server_id), config_(config) {}

double ReplicationManager::calculateServerRank(const ServerMetrics& metrics) {
    // Lower rank is better (represents load)
    return (
        config_.weights.queue_size * normalizeQueueSize(metrics.queue_size) +
        config_.weights.cpu_load * metrics.cpu_load +
        config_.weights.memory_usage * metrics.memory_usage +
        config_.weights.network_latency * normalizeLatency(metrics.network_latency)
    );
}

bool ReplicationManager::handleMessage(const Message& msg) {
    // 1. Get list of best servers for replication
    auto replica_targets = selectReplicaTargets(config_.min_replicas);
    
    // 2. If we're one of the best targets, store locally
    bool store_locally = std::find(replica_targets.begin(), 
                                 replica_targets.end(), 
                                 server_id_) != replica_targets.end();
    
    if (store_locally) {
        message_queue_.push_back(msg);
    }
    
    return store_locally;
}

std::vector<std::string> ReplicationManager::selectReplicaTargets(int count) {
    std::vector<std::pair<std::string, double>> server_ranks;
    
    // Calculate ranks for all servers including self
    for (const auto& [server_id, metrics] : server_metrics_) {
        server_ranks.emplace_back(server_id, calculateServerRank(metrics));
    }
    
    // Add self if not in metrics
    if (server_metrics_.find(server_id_) == server_metrics_.end()) {
        ServerMetrics self_metrics = getCurrentMetrics(); // Implement this to get local metrics
        server_ranks.emplace_back(server_id_, calculateServerRank(self_metrics));
    }
    
    // Sort by rank (lower is better)
    std::sort(server_ranks.begin(), server_ranks.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    // Select top N servers
    std::vector<std::string> targets;
    for (int i = 0; i < std::min(count, (int)server_ranks.size()); i++) {
        targets.push_back(server_ranks[i].first);
    }
    
    return targets;
}

bool ReplicationManager::shouldStealWork() {
    auto self_metrics = getCurrentMetrics();
    double self_rank = calculateServerRank(self_metrics);
    
    // Find average rank of all servers
    double total_rank = 0;
    int count = 0;
    for (const auto& [_, metrics] : server_metrics_) {
        total_rank += calculateServerRank(metrics);
        count++;
    }
    
    if (count == 0) return false;
    
    double avg_rank = total_rank / count;
    // If we're significantly less loaded than average, try to steal
    return self_rank < (avg_rank * config_.stealing_threshold);
}

std::vector<Message> ReplicationManager::handleStealRequest(int requested_count) {
    if (message_queue_.empty()) return {};
    
    // Calculate how many messages we can give away while maintaining min_replicas
    int available = std::max(0, (int)message_queue_.size() - config_.min_replicas);
    int to_steal = std::min(requested_count, available);
    
    std::vector<Message> stolen_messages;
    stolen_messages.reserve(to_steal);
    
    // Take messages from the front of the queue
    for (int i = 0; i < to_steal; i++) {
        stolen_messages.push_back(message_queue_.front());
        message_queue_.erase(message_queue_.begin());
    }
    
    return stolen_messages;
}

// Helper functions
double ReplicationManager::normalizeQueueSize(int size) {
    const int MAX_QUEUE_SIZE = 1000; // Can be configured
    return static_cast<double>(size) / MAX_QUEUE_SIZE;
}

double ReplicationManager::normalizeLatency(double latency) {
    const double MAX_LATENCY = 1000.0; // 1 second
    return std::min(1.0, latency / MAX_LATENCY);
}

// Add getCurrentMetrics implementation
ReplicationManager::ServerMetrics ReplicationManager::getCurrentMetrics() {
    ServerMetrics metrics;
    metrics.cpu_load = 0.5;  // Example values
    metrics.queue_size = message_queue_.size();
    metrics.memory_usage = 0.4;
    metrics.network_latency = 50.0;
    metrics.last_update = std::chrono::system_clock::now();
    return metrics;
}
