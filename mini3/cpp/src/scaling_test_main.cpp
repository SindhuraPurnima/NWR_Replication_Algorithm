#include "ScalingTest.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>

void printTestResults(const ScalingTest::TestResult& result, const std::string& test_type) {
    std::cout << "\n=== " << test_type << " Test Results ===\n";
    std::cout << "Messages Processed: " << result.messages_processed << "\n";
    std::cout << "Average Latency: " << result.average_latency << " ms\n";
    std::cout << "Equalization Rate: " << result.equalization_rate << "\n";
    std::cout << "Stability Score: " << result.stability_score << "\n";
    std::cout << "Load Imbalance: " << result.load_imbalance << "\n";
    
    std::cout << "\nScaling Efficiency:\n";
    for (const auto& [servers, efficiency] : result.scaling_efficiency) {
        std::cout << servers << " servers: " << efficiency * 100 << "%\n";
    }
}

void saveResultsToCSV(const ScalingTest::TestResult& result, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }
    
    // Write header
    file << "servers,efficiency,messages_processed,latency,equalization,stability,imbalance\n";
    
    // Write data
    for (const auto& [servers, efficiency] : result.scaling_efficiency) {
        file << servers << ","
             << efficiency << ","
             << result.messages_processed << ","
             << result.average_latency << ","
             << result.equalization_rate << ","
             << result.stability_score << ","
             << result.load_imbalance << "\n";
    }
    
    file.close();
}

int main() {
    // Configure test parameters
    ScalingTest::TestConfig config;
    config.initial_servers = 2;
    config.max_servers = 8;
    config.messages_per_second = 1000;
    config.test_duration_seconds = 30;
    config.weak_scaling_steps = 5;
    config.strong_scaling_steps = 4;
    
    // Configure replication parameters
    config.replication_config.max_queue_size = 1000;
    config.replication_config.stealing_threshold = 800;
    config.replication_config.stealing_batch_size = 100;
    config.replication_config.metrics_interval_ms = 1000;
    config.replication_config.stealing_interval_ms = 5000;
    config.replication_config.max_steal_hops = 3;
    config.replication_config.max_steal_attempts = 3;
    config.replication_config.steal_cooldown_ms = 1000;
    config.replication_config.equalization_threshold = 0.1;
    config.replication_config.stability_window = 10;
    
    // Create and run tests
    ScalingTest test(config);
    
    std::cout << "Running weak scaling test...\n";
    auto weak_result = test.runWeakScalingTest();
    printTestResults(weak_result, "Weak Scaling");
    saveResultsToCSV(weak_result, "weak_scaling_results.csv");
    
    std::cout << "\nRunning strong scaling test...\n";
    auto strong_result = test.runStrongScalingTest();
    printTestResults(strong_result, "Strong Scaling");
    saveResultsToCSV(strong_result, "strong_scaling_results.csv");
    
    return 0;
} 