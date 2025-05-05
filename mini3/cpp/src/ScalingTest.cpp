#include "ScalingTest.h"
#include <random>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>

ScalingTest::ScalingTest(const TestConfig& config)
    : config_(config) {}

void ScalingTest::setupServers(int count) {
    cleanupServers();
    servers_.reserve(count);
    
    for (int i = 0; i < count; ++i) {
        auto config = config_.replication_config;
        config.server_id = "server_" + std::to_string(i);
        config.address = "localhost";
        config.port = 50051 + i;  // Different port for each server
        
        servers_.push_back(std::make_unique<ReplicationManager>(config));
        servers_.back()->start();
    }
    
    // Wait for servers to initialize and discover each other
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void ScalingTest::cleanupServers() {
    for (auto& server : servers_) {
        server->stop();
    }
    servers_.clear();
}

std::vector<CollisionData> ScalingTest::generateTestMessages(int count) {
    std::vector<CollisionData> messages;
    messages.reserve(count);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    
    for (int i = 0; i < count; ++i) {
        CollisionData message;
        message.set_id("msg_" + std::to_string(i));
        message.set_timestamp(std::chrono::system_clock::now().time_since_epoch().count());
        message.set_priority(dis(gen));
        messages.push_back(message);
    }
    
    return messages;
}

void ScalingTest::simulateLoad(int messages_per_second, int duration_seconds) {
    auto messages = generateTestMessages(messages_per_second * duration_seconds);
    int messages_per_server = messages.size() / servers_.size();
    
    // Distribute messages across servers
    for (size_t i = 0; i < servers_.size(); ++i) {
        int start_idx = i * messages_per_server;
        int end_idx = (i + 1) * messages_per_server;
        
        for (int j = start_idx; j < end_idx; ++j) {
            servers_[i]->addMessage(messages[j]);
        }
    }
}

void ScalingTest::waitForEqualization() {
    const int max_attempts = 10;
    const int wait_seconds = 2;
    
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        bool is_equalized = true;
        double avg_queue_size = 0;
        
        // Calculate average queue size
        for (const auto& server : servers_) {
            avg_queue_size += server->getQueueSize();
        }
        avg_queue_size /= servers_.size();
        
        // Check if all servers are within threshold
        for (const auto& server : servers_) {
            double diff = std::abs(server->getQueueSize() - avg_queue_size);
            if (diff > config_.replication_config.equalization_threshold) {
                is_equalized = false;
                break;
            }
        }
        
        if (is_equalized) break;
        std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));
    }
}

void ScalingTest::collectMetrics(TestResult& result) {
    result.messages_processed = 0;
    result.average_latency = 0;
    result.equalization_rate = 0;
    result.stability_score = 0;
    result.load_imbalance = 0;
    
    for (const auto& server : servers_) {
        auto metrics = server->getFairnessMetrics();
        result.messages_processed += server->getQueueSize();
        result.equalization_rate += metrics.equalization_rate;
        result.stability_score += metrics.stability_score;
        result.load_imbalance += metrics.load_imbalance;
    }
    
    result.messages_processed /= servers_.size();
    result.equalization_rate /= servers_.size();
    result.stability_score /= servers_.size();
    result.load_imbalance /= servers_.size();
}

double ScalingTest::calculateScalingEfficiency(const TestResult& result, int server_count) {
    if (server_count <= 1) return 1.0;
    
    // Calculate speedup (messages processed per server)
    double speedup = result.messages_processed / (result.messages_processed / server_count);
    
    // Calculate efficiency (speedup / number of servers)
    return speedup / server_count;
}

ScalingTest::TestResult ScalingTest::runWeakScalingTest() {
    TestResult result;
    setupServers(config_.initial_servers);
    
    int step_size = config_.messages_per_second / config_.weak_scaling_steps;
    int current_load = step_size;
    
    for (int step = 0; step < config_.weak_scaling_steps; ++step) {
        test_start_ = std::chrono::steady_clock::now();
        simulateLoad(current_load, config_.test_duration_seconds);
        waitForEqualization();
        
        TestResult step_result;
        collectMetrics(step_result);
        step_result.scaling_efficiency[config_.initial_servers] = 
            calculateScalingEfficiency(step_result, config_.initial_servers);
        
        // Update overall result
        result.messages_processed += step_result.messages_processed;
        result.average_latency += step_result.average_latency;
        result.equalization_rate += step_result.equalization_rate;
        result.stability_score += step_result.stability_score;
        result.load_imbalance += step_result.load_imbalance;
        
        current_load += step_size;
    }
    
    // Calculate averages
    result.messages_processed /= config_.weak_scaling_steps;
    result.average_latency /= config_.weak_scaling_steps;
    result.equalization_rate /= config_.weak_scaling_steps;
    result.stability_score /= config_.weak_scaling_steps;
    result.load_imbalance /= config_.weak_scaling_steps;
    
    cleanupServers();
    return result;
}

ScalingTest::TestResult ScalingTest::runStrongScalingTest() {
    TestResult result;
    int step_size = (config_.max_servers - config_.initial_servers) / config_.strong_scaling_steps;
    int current_servers = config_.initial_servers;
    
    for (int step = 0; step < config_.strong_scaling_steps; ++step) {
        setupServers(current_servers);
        test_start_ = std::chrono::steady_clock::now();
        
        simulateLoad(config_.messages_per_second, config_.test_duration_seconds);
        waitForEqualization();
        
        TestResult step_result;
        collectMetrics(step_result);
        step_result.scaling_efficiency[current_servers] = 
            calculateScalingEfficiency(step_result, current_servers);
        
        // Update overall result
        result.messages_processed += step_result.messages_processed;
        result.average_latency += step_result.average_latency;
        result.equalization_rate += step_result.equalization_rate;
        result.stability_score += step_result.stability_score;
        result.load_imbalance += step_result.load_imbalance;
        
        cleanupServers();
        current_servers += step_size;
    }
    
    // Calculate averages
    result.messages_processed /= config_.strong_scaling_steps;
    result.average_latency /= config_.strong_scaling_steps;
    result.equalization_rate /= config_.strong_scaling_steps;
    result.stability_score /= config_.strong_scaling_steps;
    result.load_imbalance /= config_.strong_scaling_steps;
    
    return result;
} 