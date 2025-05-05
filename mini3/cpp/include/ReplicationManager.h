#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <memory>
#include <optional>
#include <queue>
#include <chrono>
#include <map>
#include <grpcpp/grpcpp.h>
#include "proto/mini2.grpc.pb.h"
#include "MetricsCollector.h"

using namespace mini2;

class ReplicationManager {
public:
    struct Weights {
        double queue_size = 0.3;
        double cpu_load = 0.2;
        double memory_usage = 0.2;
        double network_latency = 0.2;
        double distance = 0.1;
    };

    struct Config {
        std::string server_id;
        std::string address;
        int port;
        int max_queue_size = 1000;
        int min_queue_size = 100;
        double steal_threshold = 0.7;
        int max_steal_distance = 3;
        int stability_window = 10;  // Number of metrics to consider for stability
        double queue_size_weight = 0.3;
        double cpu_utilization_weight = 0.2;
        double memory_usage_weight = 0.2;
        double network_latency_weight = 0.1;
        double hop_distance_weight = 0.1;
        double steal_history_weight = 0.1;
        double stealing_threshold = 0.7;
        int stealing_batch_size = 10;
        int metrics_interval_ms = 1000;
        int stealing_interval_ms = 5000;
        int min_replicas = 3;
        int max_steal_attempts = 3;
        int max_steal_hops = 2;
        int steal_cooldown_ms = 1000;
        
        // Network simulation parameters
        double base_latency = 50.0;
        double max_acceptable_latency = 100.0;
        double latency_variation = 20.0;
        std::map<std::string, int> topology;  // Server ID -> hop distance
        
        std::vector<ServerInfo> servers;
    };

    struct ServerRank {
        std::string server_id;
        double score;
        int distance;
        ServerMetrics metrics;
    };

    struct FairnessMetrics {
        double equalization_rate = 0.0;
        double stability_score = 0.0;
        double load_imbalance = 0.0;
        double message_consistency = 0.0;
    };

    struct TimestampedMessage {
        CollisionData message;
        std::chrono::system_clock::time_point timestamp;
        int steal_count;
        std::string last_stealer;
    };

    explicit ReplicationManager(const Config& config, const std::string& server_id);
    ~ReplicationManager();

    void start();
    void stop();
    bool addMessage(const CollisionData& message);
    bool getMessage(CollisionData& message);
    size_t getQueueSize() const;
    double getAverageMessageAge() const;
    double getMaxMessageAge() const;
    ServerMetrics getCurrentMetrics() const;
    void setWeights(const Weights& weights);

    void updateServerMetrics(const std::string& server_id, const ServerMetrics& metrics);
    void removeServerMetrics(const std::string& server_id);
    std::optional<ServerMetrics> getServerMetrics(const std::string& server_id) const;

    void updateServerRanks();
    std::vector<std::string> getServerRanks() const;
    double getServerRank(const std::string& server_id) const;

    void stealWorkFromOtherServers();
    void handleStolenWork(const std::vector<CollisionData>& stolen_work);
    bool handleMessage(const CollisionData& msg);
    std::vector<std::string> selectReplicaTargets(int count);
    bool shouldStealWork() const;
    std::vector<CollisionData> handleStealRequest(int requested_count);

    // Metrics operations
    FairnessMetrics getFairnessMetrics() const;
    void updateServerMetrics(const ServerInfo& server);
    void removeServer(const std::string& server_id);

    // Work stealing
    void startWorkStealing();
    void stopWorkStealing();
    std::optional<ServerInfo> findBestServerToStealFrom();
    void stealWorkFromServer(const ServerInfo& server);
    double calculateServerRank(const ServerInfo& server) const;
    bool canStealFromServer(const ServerInfo& server) const;
    void updateNetworkTopology(const ServerInfo& server);
    void updateNetworkLatency(const std::string& server_id, double latency);

private:
    Config config_;
    Weights weights_;
    MetricsCollector metrics_collector_;
    std::queue<TimestampedMessage> message_queue_;
    size_t max_queue_size_;
    std::vector<ServerInfo> known_servers_;
    std::map<std::string, std::chrono::system_clock::time_point> last_steal_time_;
    std::map<std::string, int> server_distances_;
    std::vector<std::vector<ServerMetrics>> metrics_history_;  // History for stability calculation
    FairnessMetrics fairness_metrics_;
    mutable std::mutex queue_mutex_;
    mutable std::mutex servers_mutex_;
    mutable std::mutex topology_mutex_;
    mutable std::mutex metrics_mutex_;
    std::thread metrics_thread_;
    std::thread stealing_thread_;
    std::thread fairness_thread_;
    bool running_;
    std::map<std::string, ServerMetrics> server_metrics_;
    std::map<std::string, double> server_ranks_;
    ServerMetrics current_metrics_;

    // Metrics collection and management
    void metricsCollectionLoop();
    void workStealingLoop();
    void fairnessMetricsLoop();
    void updateMetrics();
    void updateFairnessMetrics();
    double calculateEqualizationRate() const;
    double calculateStabilityScore() const;
    double calculateLoadImbalance() const;
    double calculateMessageConsistency() const;

    // Helper functions
    double getCpuLoad() const;
    double getMemoryUsage() const;
    double measureAverageLatency() const;
    double calculateServerRank(const ServerMetrics& metrics) const;
    double normalizeQueueSize(int size) const;
    double normalizeLatency(double latency) const;
    double normalizeDistance(double distance) const;
    int calculateDistance(const std::string& server_id) const;
    std::vector<std::string> selectTargetServers() const;
    void updateServerDistances();
};
