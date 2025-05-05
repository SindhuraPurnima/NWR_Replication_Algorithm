#include "MetricsCollector.h"
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <sys/resource.h>
#include <mutex>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <sys/sysctl.h>
#include <unistd.h>

MetricsCollector::MetricsCollector(const std::string& server_id)
    : server_id_(server_id),
      total_records_processed_(0),
      last_collection_(std::chrono::system_clock::now()),
      simulated_network_latency_(50.0),  // Default 50ms latency
      max_acceptable_latency_(100.0),    // Default 100ms max acceptable
      simulated_hop_distance_(0) {        // Default 0 hops (self)
    // Initialize work steal and records forwarded counts
    work_steal_counts_[server_id] = 0;
    records_forwarded_counts_[server_id] = 0;
}

MetricsCollector::~MetricsCollector() {
    // Clean up any resources if needed
}

double MetricsCollector::getCpuLoad() {
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, 
                       (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        // Calculate CPU usage as a percentage
        unsigned long total = cpuinfo.cpu_ticks[CPU_STATE_USER] +
                            cpuinfo.cpu_ticks[CPU_STATE_SYSTEM] +
                            cpuinfo.cpu_ticks[CPU_STATE_IDLE] +
                            cpuinfo.cpu_ticks[CPU_STATE_NICE];
        if (total > 0) {
            unsigned long used = total - cpuinfo.cpu_ticks[CPU_STATE_IDLE];
            return static_cast<double>(used) / total;
        }
    }
    return 0.0;
}

ServerMetrics MetricsCollector::collectMetrics(int queue_size, int max_queue_size) {
    ServerMetrics metrics;
    
    // Get CPU usage (real-time)
    metrics.set_cpu_utilization(getCpuLoad());
    
    // Get memory usage (real-time)
    metrics.set_memory_usage(getMemoryUsage());
    
    // Use simulated network latency (configurable)
    metrics.set_avg_network_latency(simulated_network_latency_);
    
    // Use real queue length (passed as parameter)
    metrics.set_message_queue_length(queue_size);
    metrics.set_max_queue_length(max_queue_size);
    
    // Set hop distance (configurable)
    metrics.set_hop_distance(simulated_hop_distance_);
    
    // Set other metrics
    metrics.set_max_acceptable_latency(max_acceptable_latency_);
    
    // Store metrics in history for later analysis
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_history_.push_back(metrics);
    
    // Trim history if too large
    if (metrics_history_.size() > 100) {
        metrics_history_.erase(metrics_history_.begin());
    }
    
    return metrics;
}

double MetricsCollector::getMemoryUsage() {
    vm_size_t page_size;
    mach_port_t mach_port;
    mach_msg_type_number_t count;
    vm_statistics64_data_t vm_stats;
    
    mach_port = mach_host_self();
    count = sizeof(vm_stats) / sizeof(natural_t);
    if (KERN_SUCCESS == host_page_size(mach_port, &page_size) &&
        KERN_SUCCESS == host_statistics64(mach_port, HOST_VM_INFO64,
                                        (host_info64_t)&vm_stats, &count)) {
        long long free_memory = (int64_t)vm_stats.free_count * (int64_t)page_size;
        long long used_memory = ((int64_t)vm_stats.active_count +
                               (int64_t)vm_stats.inactive_count +
                               (int64_t)vm_stats.wire_count) * (int64_t)page_size;
        long long total_memory = free_memory + used_memory;
        
        if (total_memory > 0) {
            return static_cast<double>(used_memory) / total_memory;
        }
    }
    return 0.0;
}

void MetricsCollector::setSimulatedNetworkLatency(double latency) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    simulated_network_latency_ = latency;
}

void MetricsCollector::setSimulatedHopDistance(int hop_distance) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    simulated_hop_distance_ = hop_distance;
}

double MetricsCollector::measureNetworkLatency(const std::string& target_server) {
    // This is now for testing or manual benchmarking
    // In a real system, you'd ping the server or measure gRPC latency
    return simulated_network_latency_;
}

void MetricsCollector::recordWorkSteal(const std::string& source_server, const std::string& target_server) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    work_steal_counts_[source_server + "->" + target_server]++;
}

void MetricsCollector::recordRecordProcessed() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    total_records_processed_++;
}

void MetricsCollector::recordRecordForwarded(const std::string& target_server) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    records_forwarded_counts_[target_server]++;
}

void MetricsCollector::generatePerformanceReport(const std::string& filename) {
    std::ofstream report(filename);
    if (!report.is_open()) {
        std::cerr << "Failed to open report file: " << filename << std::endl;
        return;
    }

    auto metrics = calculatePerformanceMetrics();
    
    report << "Performance Report for Server: " << server_id_ << "\n\n";
    report << "Throughput: " << metrics.throughput << " records/second\n";
    report << "Average Response Time: " << metrics.avg_response_time << " ms\n";
    report << "Load Balance Score: " << metrics.load_balance_score << "\n";
    report << "Consistency Score: " << metrics.consistency_score << "\n\n";
    
    report << "Work Stealing Events:\n";
    for (const auto& pair : work_steal_counts_) {
        report << "  " << pair.first << ": " << pair.second << " times\n";
    }
    
    report << "\nRecords Forwarded:\n";
    for (const auto& pair : records_forwarded_counts_) {
        report << "  To " << pair.first << ": " << pair.second << " records\n";
    }
    
    report.close();
}

MetricsCollector::PerformanceMetrics MetricsCollector::calculatePerformanceMetrics() {
    PerformanceMetrics metrics;
    
    // Calculate throughput
    if (!metrics_history_.empty()) {
        auto time_span = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - last_collection_).count();
        if (time_span > 0) {
            metrics.throughput = static_cast<double>(total_records_processed_) / time_span;
        }
    }
    
    // Calculate load balance score
    metrics.load_balance_score = calculateLoadBalanceScore();
    
    // Calculate consistency score
    metrics.consistency_score = calculateConsistencyScore();
    
    return metrics;
}

void MetricsCollector::logMetrics(const ServerMetrics& metrics) {
    std::ofstream log("metrics_log.csv", std::ios::app);
    if (!log.is_open()) {
        std::cerr << "Failed to open metrics log file" << std::endl;
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    log << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S") << ","
        << metrics.cpu_utilization() << ","
        << metrics.memory_usage() << ","
        << metrics.avg_network_latency() << ","
        << metrics.message_queue_length() << ","
        << metrics.avg_message_age() << "\n";
    
    log.close();
}

bool MetricsCollector::verifyDataConsistency(const std::vector<std::string>& server_ids) {
    // Implementation would depend on your data consistency requirements
    // This is a placeholder that assumes consistency if all servers have processed similar numbers of records
    if (metrics_history_.empty()) return true;
    
    auto latest = metrics_history_.back();
    double avg_records = static_cast<double>(total_records_processed_) / server_ids.size();
    double variance = 0.0;
    
    for (const auto& metrics : metrics_history_) {
        variance += std::pow(metrics.message_queue_length() - avg_records, 2);
    }
    variance /= metrics_history_.size();
    
    return variance < (avg_records * 0.1); // Allow 10% variance
}

void MetricsCollector::resolveConflicts(const std::vector<std::string>& server_ids) {
    // Implementation would depend on your conflict resolution strategy
    // This is a placeholder that logs conflicts
    std::ofstream conflict_log("conflicts.log", std::ios::app);
    if (!conflict_log.is_open()) {
        std::cerr << "Failed to open conflict log file" << std::endl;
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    conflict_log << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S")
                << " - Conflict resolution initiated for servers: ";
    for (const auto& id : server_ids) {
        conflict_log << id << " ";
    }
    conflict_log << "\n";
    
    conflict_log.close();
}

double MetricsCollector::calculateLoadBalanceScore() {
    if (metrics_history_.empty()) return 1.0;
    
    double total_load = 0.0;
    for (const auto& metrics : metrics_history_) {
        total_load += metrics.cpu_utilization() + metrics.memory_usage();
    }
    double avg_load = total_load / metrics_history_.size();
    
    double variance = 0.0;
    for (const auto& metrics : metrics_history_) {
        double current_load = metrics.cpu_utilization() + metrics.memory_usage();
        variance += std::pow(current_load - avg_load, 2);
    }
    variance /= metrics_history_.size();
    
    // Score closer to 1.0 means better load balancing
    return 1.0 / (1.0 + variance);
}

double MetricsCollector::calculateConsistencyScore() {
    if (metrics_history_.empty()) return 1.0;
    
    // Calculate consistency based on record processing rates
    double total_records = 0.0;
    for (const auto& metrics : metrics_history_) {
        total_records += metrics.message_queue_length();
    }
    double avg_records = total_records / metrics_history_.size();
    
    double variance = 0.0;
    for (const auto& metrics : metrics_history_) {
        variance += std::pow(metrics.message_queue_length() - avg_records, 2);
    }
    variance /= metrics_history_.size();
    
    // Score closer to 1.0 means better consistency
    return 1.0 / (1.0 + variance);
}

// Add the implementation of getAllServerMetrics
std::vector<ServerInfo> MetricsCollector::getAllServerMetrics() const {
    // Return a copy of the stored server metrics
    return server_metrics_; // Remove the lock_guard for const methods
}

// Add the implementation of getServerMetrics
std::optional<ServerInfo> MetricsCollector::getServerMetrics(const std::string& server_id) const {
    // Remove the lock_guard for const methods
    for (const auto& server : server_metrics_) {
        if (server.server_id() == server_id) {
            return server;
        }
    }
    return std::nullopt;
}

// Add the implementation of updateServerMetrics
void MetricsCollector::updateServerMetrics(const ServerInfo& server) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Find existing server metrics
    auto it = std::find_if(server_metrics_.begin(), server_metrics_.end(),
                         [&server](const ServerInfo& s) {
                             return s.server_id() == server.server_id();
                         });
    
    if (it != server_metrics_.end()) {
        // Update existing entry
        *it = server;
    } else {
        // Add new entry
        server_metrics_.push_back(server);
    }
}

// Add the implementation of removeServerMetrics
void MetricsCollector::removeServerMetrics(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Clean up any metrics for this server
    auto it = std::find_if(server_metrics_.begin(), server_metrics_.end(), 
                         [&server_id](const ServerInfo& info) {
                             return info.server_id() == server_id;
                         });
    if (it != server_metrics_.end()) {
        server_metrics_.erase(it);
    }
    
    // Clean up any work steal counts related to this server
    std::vector<std::string> keys_to_remove;
    for (const auto& [key, _] : work_steal_counts_) {
        if (key.find(server_id) != std::string::npos) {
            keys_to_remove.push_back(key);
        }
    }
    
    for (const auto& key : keys_to_remove) {
        work_steal_counts_.erase(key);
    }
    
    // Clean up any record forwarding counts
    records_forwarded_counts_.erase(server_id);
}
