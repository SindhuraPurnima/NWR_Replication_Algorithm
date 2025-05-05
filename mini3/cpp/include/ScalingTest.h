#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include <map>
#include "ReplicationManager.h"
#include "proto/mini2.grpc.pb.h"

class ScalingTest {
public:
    struct TestConfig {
        int initial_servers;
        int max_servers;
        int messages_per_second;
        int test_duration_seconds;
        int weak_scaling_steps;
        int strong_scaling_steps;
        ReplicationManager::Config replication_config;
    };

    struct TestResult {
        double messages_processed;
        double average_latency;
        double equalization_rate;
        double stability_score;
        double load_imbalance;
        std::map<int, double> scaling_efficiency;  // server count -> efficiency
    };

    explicit ScalingTest(const TestConfig& config);
    
    // Run weak scaling test (increase load while keeping servers constant)
    TestResult runWeakScalingTest();
    
    // Run strong scaling test (increase servers while keeping load constant)
    TestResult runStrongScalingTest();
    
    // Generate test messages
    std::vector<CollisionData> generateTestMessages(int count);
    
    // Measure performance metrics
    void measurePerformance(TestResult& result);

private:
    TestConfig config_;
    std::vector<std::unique_ptr<ReplicationManager>> servers_;
    std::chrono::steady_clock::time_point test_start_;
    
    // Helper functions
    void setupServers(int count);
    void cleanupServers();
    void simulateLoad(int messages_per_second, int duration_seconds);
    double calculateScalingEfficiency(const TestResult& result, int server_count);
    void collectMetrics(TestResult& result);
    void waitForEqualization();
}; 