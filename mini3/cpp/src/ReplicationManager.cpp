#include "ReplicationManager.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <mutex>

using namespace mini2;

ReplicationManager::ReplicationManager(const Config& config, const std::string& server_id)
    : config_(config), 
      metrics_collector_(server_id),
      max_queue_size_(config.max_queue_size), 
      running_(false) {
    // Initialize server distances
    server_distances_[config.server_id] = 0;
    // Initialize weights
    weights_.queue_size = 0.3;
    weights_.cpu_load = 0.2;
    weights_.memory_usage = 0.2;
    weights_.network_latency = 0.2;
    weights_.distance = 0.1;
    // Initialize steal-related fields
    config_.max_steal_attempts = 3;  // Default value
    config_.max_steal_hops = 2;      // Default value
    config_.steal_cooldown_ms = 1000; // 1 second cooldown
    
    // Initialize current metrics
    current_metrics_.set_cpu_utilization(0.0);
    current_metrics_.set_message_queue_length(0);
    current_metrics_.set_memory_usage(0.0);
    current_metrics_.set_avg_network_latency(0.0);
    current_metrics_.set_max_queue_length(config.max_queue_size);
    current_metrics_.set_hop_distance(0);

    // Start metrics collection thread
    metrics_thread_ = std::thread(&ReplicationManager::metricsCollectionLoop, this);
    
    // Start work stealing thread
    stealing_thread_ = std::thread(&ReplicationManager::workStealingLoop, this);
    
    // Start fairness metrics thread
    fairness_thread_ = std::thread(&ReplicationManager::fairnessMetricsLoop, this);
    
    running_ = true;
}

ReplicationManager::~ReplicationManager() {
    stop();
}

void ReplicationManager::start() {
    if (running_) return;
    running_ = true;
    // Start metrics collection thread
    metrics_thread_ = std::thread(&ReplicationManager::metricsCollectionLoop, this);
    // Start work stealing thread
    stealing_thread_ = std::thread(&ReplicationManager::workStealingLoop, this);
    fairness_thread_ = std::thread(&ReplicationManager::fairnessMetricsLoop, this);
}

void ReplicationManager::stop() {
    if (!running_) return;
    running_ = false;
    if (metrics_thread_.joinable()) {
        metrics_thread_.join();
    }
    if (stealing_thread_.joinable()) {
        stealing_thread_.join();
    }
    if (fairness_thread_.joinable()) {
        fairness_thread_.join();
    }
}

bool ReplicationManager::addMessage(const CollisionData& message) {
    std::lock_guard<std::mutex> lock{queue_mutex_};
    if (message_queue_.size() >= config_.max_queue_size) {
        return false;
    }
    
    TimestampedMessage tm;
    tm.message = message;
    tm.timestamp = std::chrono::system_clock::now();
    tm.steal_count = 0;
    tm.last_stealer = "";
    
    message_queue_.push(tm);
    return true;
}

bool ReplicationManager::getMessage(CollisionData& message) {
    std::lock_guard<std::mutex> lock{queue_mutex_};
    if (message_queue_.empty()) {
        return false;
    }
    
    auto& tm = message_queue_.front();
    if (tm.steal_count >= config_.max_steal_attempts) {
        message_queue_.pop();
        return false;
    }
    
    message = tm.message;
    message_queue_.pop();
    return true;
}

size_t ReplicationManager::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return message_queue_.size();
}

double ReplicationManager::getAverageMessageAge() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (message_queue_.empty()) {
        return 0.0;
    }

    auto now = std::chrono::system_clock::now();
    double total_age = 0.0;
    std::queue<TimestampedMessage> temp_queue = message_queue_;

    while (!temp_queue.empty()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - temp_queue.front().timestamp
        ).count();
        total_age += age;
        temp_queue.pop();
    }

    return total_age / message_queue_.size();
}

double ReplicationManager::getMaxMessageAge() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (message_queue_.empty()) {
        return 0.0;
    }

    auto now = std::chrono::system_clock::now();
    double max_age = 0.0;
    std::queue<TimestampedMessage> temp_queue = message_queue_;

    while (!temp_queue.empty()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - temp_queue.front().timestamp
        ).count();
        max_age = std::max(max_age, static_cast<double>(age));
        temp_queue.pop();
    }

    return max_age;
}

ServerMetrics ReplicationManager::getCurrentMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return current_metrics_;
}

void ReplicationManager::metricsCollectionLoop() {
    while (running_) {
        updateMetrics();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.metrics_interval_ms));
    }
}

void ReplicationManager::workStealingLoop() {
    while (running_) {
        if (shouldStealWork()) {
            stealWorkFromOtherServers();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.stealing_interval_ms));
    }
}

void ReplicationManager::fairnessMetricsLoop() {
    while (running_) {
        updateFairnessMetrics();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.metrics_interval_ms));
    }
}

void ReplicationManager::updateMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Get the real-time queue size
    size_t current_queue_size = getQueueSize();
    
    // Use real queue size when collecting metrics
    current_metrics_ = metrics_collector_.collectMetrics(
        current_queue_size,
        config_.max_queue_size
    );
    
    // Update hop distance for other servers based on network topology
    for (const auto& [server_id, distance] : server_distances_) {
        metrics_collector_.setSimulatedHopDistance(distance);
    }
    
    // Add message age information
    current_metrics_.set_avg_message_age(getAverageMessageAge());
    current_metrics_.set_max_message_age(getMaxMessageAge());
}

void ReplicationManager::updateFairnessMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    fairness_metrics_.equalization_rate = calculateEqualizationRate();
    fairness_metrics_.stability_score = calculateStabilityScore();
    fairness_metrics_.load_imbalance = calculateLoadImbalance();
    fairness_metrics_.message_consistency = calculateMessageConsistency();
}

double ReplicationManager::calculateEqualizationRate() const {
    // Implementation of equalization rate calculation
    return 0.0;
}

double ReplicationManager::calculateStabilityScore() const {
    // Implementation of stability score calculation
    return 0.0;
}

double ReplicationManager::calculateLoadImbalance() const {
    // Implementation of load imbalance calculation
    return 0.0;
}

double ReplicationManager::calculateMessageConsistency() const {
    // Implementation of message consistency calculation
    return 0.0;
}

void ReplicationManager::updateNetworkTopology(const ServerInfo& server) {
    std::lock_guard<std::mutex> lock(topology_mutex_);
    
    // Update distance for the server
    int new_distance = calculateDistance(server.server_id());
    server_distances_[server.server_id()] = new_distance;
    
    // Update distances for all known servers
    for (const auto& known_server : known_servers_) {
        if (known_server.server_id() != server.server_id()) {
            int distance = calculateDistance(known_server.server_id());
            server_distances_[known_server.server_id()] = distance;
        }
    }
}

int ReplicationManager::calculateDistance(const std::string& server_id) const {
    if (server_id == config_.server_id) {
        return 0;
    }
    
    // Simple distance calculation based on server ID similarity
    // In a real implementation, this would use network topology information
    int distance = 0;
    size_t min_length = std::min(server_id.length(), config_.server_id.length());
    for (size_t i = 0; i < min_length; ++i) {
        if (server_id[i] != config_.server_id[i]) {
            ++distance;
        }
    }
    distance += std::abs(static_cast<int>(server_id.length() - config_.server_id.length()));
    
    return distance;
}

double ReplicationManager::calculateServerRank(const ServerMetrics& metrics) const {
    // Calculate rank based on metrics
    double queue_score = normalizeQueueSize(metrics.message_queue_length());
    double cpu_score = 1.0 - metrics.cpu_utilization();
    double memory_score = 1.0 - metrics.memory_usage();
    double latency_score = normalizeLatency(metrics.avg_network_latency());
    
    return weights_.queue_size * queue_score +
           weights_.cpu_load * cpu_score +
           weights_.memory_usage * memory_score +
           weights_.network_latency * latency_score;
}

double ReplicationManager::calculateServerRank(const ServerInfo& server) const {
    const auto& metrics = server.metrics();
    int distance = calculateDistance(server.server_id());
    double distance_score = normalizeDistance(distance);
    
    return calculateServerRank(metrics) + weights_.distance * distance_score;
}

bool ReplicationManager::canStealFromServer(const ServerInfo& server) const {
    // Check if server is within max hop distance
    int distance = calculateDistance(server.server_id());
    if (distance > config_.max_steal_hops) {
        return false;
    }
    
    // Check cooldown period
    auto now = std::chrono::system_clock::now();
    auto last_steal = last_steal_time_.find(server.server_id());
    if (last_steal != last_steal_time_.end()) {
        auto time_since_last_steal = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_steal->second
        ).count();
        if (time_since_last_steal < config_.steal_cooldown_ms) {
            return false;
        }
    }
    
    return true;
}

std::optional<ServerInfo> ReplicationManager::findBestServerToStealFrom() {
    std::lock_guard<std::mutex> lock(servers_mutex_);
    
    std::vector<ServerRank> server_ranks;
    for (const auto& server : known_servers_) {
        if (canStealFromServer(server)) {
            ServerRank rank;
            rank.server_id = server.server_id();
            rank.score = calculateServerRank(server);
            rank.distance = calculateDistance(server.server_id());
            rank.metrics = server.metrics();
            server_ranks.push_back(rank);
        }
    }
    
    if (server_ranks.empty()) {
        return std::nullopt;
    }
    
    // Sort servers by rank
    std::sort(server_ranks.begin(), server_ranks.end(),
              [](const ServerRank& a, const ServerRank& b) { return a.score > b.score; });
    
    // Find the server with the highest rank
    auto best_server = std::find_if(known_servers_.begin(), known_servers_.end(),
                                   [&](const ServerInfo& s) { return s.server_id() == server_ranks[0].server_id; });
    
    if (best_server != known_servers_.end()) {
        return *best_server;
    }
    
    return std::nullopt;
}

void ReplicationManager::stealWorkFromOtherServers() {
    std::lock_guard<std::mutex> lock(servers_mutex_);
    
    // Find the best server to steal from
    auto best_server = findBestServerToStealFrom();
    if (!best_server) {
        return;
    }
    
    // Create gRPC channel to target server
    auto channel = grpc::CreateChannel(
        best_server->address() + ":" + std::to_string(best_server->port()),
        grpc::InsecureChannelCredentials()
    );
    
    auto stub = InterServerService::NewStub(channel);
    
    // Prepare steal request
    StealRequest request;
    request.set_requester_id(config_.server_id);
    request.set_requested_count(config_.stealing_batch_size);
    *request.mutable_requester_metrics() = getCurrentMetrics();
    
    // Send steal request
    StealResponse response;
    grpc::ClientContext context;
    auto status = stub->StealWork(&context, request, &response);
    
    if (status.ok() && response.success()) {
        // Add stolen messages to our queue
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        for (const auto& message : response.stolen_messages()) {
            TimestampedMessage tm;
            tm.message = message;
            tm.timestamp = std::chrono::system_clock::now();
            tm.steal_count = 0;
            tm.last_stealer = "";
            message_queue_.push(tm);
        }
        
        // Update last steal time
        last_steal_time_[best_server->server_id()] = std::chrono::system_clock::now();
    }
}

void ReplicationManager::updateServerMetrics(const ServerInfo& server) {
    std::lock_guard<std::mutex> lock(servers_mutex_);
    
    // Update or add server info
    auto it = std::find_if(
        known_servers_.begin(),
        known_servers_.end(),
        [&](const ServerInfo& s) { return s.server_id() == server.server_id(); }
    );
    
    if (it != known_servers_.end()) {
        *it = server;
    } else {
        known_servers_.push_back(server);
    }
}

void ReplicationManager::removeServer(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(servers_mutex_);
    known_servers_.erase(
        std::remove_if(
            known_servers_.begin(),
            known_servers_.end(),
            [&](const ServerInfo& s) { return s.server_id() == server_id; }
        ),
        known_servers_.end()
    );
}

bool ReplicationManager::handleMessage(const CollisionData& msg) {
    // 1. Get list of best servers for replication
    auto replica_targets = selectReplicaTargets(config_.min_replicas);
    
    // 2. If we're one of the best targets, store locally
    bool store_locally = std::find(replica_targets.begin(), 
                                 replica_targets.end(), 
                                 config_.server_id) != replica_targets.end();
    
    if (store_locally) {
        TimestampedMessage tm;
        tm.message = msg;
        tm.timestamp = std::chrono::system_clock::now();
        tm.steal_count = 0;
        tm.last_stealer = "";
        message_queue_.push(tm);
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
    if (server_metrics_.find(config_.server_id) == server_metrics_.end()) {
        ServerMetrics self_metrics = getCurrentMetrics();
        server_ranks.emplace_back(config_.server_id, calculateServerRank(self_metrics));
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

bool ReplicationManager::shouldStealWork() const {
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

std::vector<CollisionData> ReplicationManager::handleStealRequest(int requested_count) {
    if (message_queue_.empty()) return {};
    
    // Calculate how many messages we can give away while maintaining min_replicas
    int available = std::max(0, (int)message_queue_.size() - config_.min_replicas);
    int to_steal = std::min(requested_count, available);
    
    std::vector<CollisionData> stolen_messages;
    stolen_messages.reserve(to_steal);
    
    // Take messages from the front of the queue
    for (int i = 0; i < to_steal; i++) {
        stolen_messages.push_back(message_queue_.front().message);
        message_queue_.pop();
    }
    
    return stolen_messages;
}

// Helper functions
double ReplicationManager::normalizeQueueSize(int size) const {
    const int MAX_QUEUE_SIZE = 1000;
    return static_cast<double>(size) / MAX_QUEUE_SIZE;
}

double ReplicationManager::normalizeLatency(double latency) const {
    const double MAX_LATENCY = 1000.0;
    return std::min(1.0, latency / MAX_LATENCY);
}

double ReplicationManager::getCpuLoad() const {
    // Simple implementation
    return 0.5; // 50% load for testing
}

double ReplicationManager::getMemoryUsage() const {
    // Simple implementation
    return 0.4; // 40% usage for testing
}

double ReplicationManager::measureAverageLatency() const {
    // Simplified implementation - in a real system you would measure actual latency
    // to other servers, perhaps using ping or measuring gRPC call round-trip times
    double total_latency = 0.0;
    int count = 0;
    
    std::lock_guard<std::mutex> lock(servers_mutex_);
    
    // CRITICAL FIX: Check if servers array is valid and non-empty before iterating
    if (config_.servers.empty()) {
        std::cout << "Warning: No servers configured for latency measurement. Using default latency." << std::endl;
        return 50.0; // Default latency value
    }
    
    for (const auto& server : config_.servers) {
        // CRITICAL FIX: Add safety checks before accessing server methods
        try {
            if (!server.server_id().empty() && server.server_id() != config_.server_id) {
                // Simulate different latencies for different servers
                double latency = 20.0 + (rand() % 80); // 20-100ms range
                total_latency += latency;
                count++;
                
                // Update the latency for this server
                const_cast<ReplicationManager*>(this)->updateNetworkLatency(server.server_id(), latency);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error accessing server data: " << e.what() << std::endl;
            // Continue to next server
        }
    }
    
    return count > 0 ? total_latency / count : 50.0; // Return default if no servers
}

ReplicationManager::FairnessMetrics ReplicationManager::getFairnessMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return fairness_metrics_;
}

void ReplicationManager::handleStolenWork(const std::vector<CollisionData>& stolen_work) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (const auto& message : stolen_work) {
        TimestampedMessage tm;
        tm.message = message;
        tm.timestamp = std::chrono::system_clock::now();
        tm.steal_count = 0;
        tm.last_stealer = "";
        message_queue_.push(tm);
    }
}

void ReplicationManager::updateServerDistances() {
    std::lock_guard<std::mutex> lock(topology_mutex_);
    
    // Update distances (simplified implementation)
    for (const auto& server : config_.servers) {
        if (server.server_id() == config_.server_id) {
            server_distances_[server.server_id()] = 0; // Self
        }
        else if (server_distances_.find(server.server_id()) == server_distances_.end()) {
            // New server, add at distance 1
            server_distances_[server.server_id()] = 1; // Direct connection
        }
    }
    
    // Update the metrics collector with appropriate hop distances
    for (const auto& [server_id, distance] : server_distances_) {
        if (server_id == config_.server_id) {
            metrics_collector_.setSimulatedHopDistance(0); // Self
        }
    }
}

double ReplicationManager::normalizeDistance(double distance) const {
    const double MAX_DISTANCE = 10.0;  // Maximum network distance
    return std::min(1.0, distance / MAX_DISTANCE);
}

void ReplicationManager::updateNetworkLatency(const std::string& server_id, double latency) {
    // Update the simulated latency in the metrics collector
    metrics_collector_.setSimulatedNetworkLatency(latency);
}
