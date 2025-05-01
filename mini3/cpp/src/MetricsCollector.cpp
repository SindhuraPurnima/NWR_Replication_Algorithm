#include "MetricsCollector.h"
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <thread>

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

ServerMetrics MetricsCollector::collectMetrics() {
    ServerMetrics metrics;
    metrics.cpu_load = getCpuLoad();
    metrics.memory_usage = getMemoryUsage();
    metrics.queue_size = 0;  // This should be set by the caller
    metrics.network_latency = measureNetworkLatency("localhost");
    metrics.last_update = std::chrono::system_clock::now();
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

double MetricsCollector::measureNetworkLatency(const std::string& target_server) {
    // Simple implementation for testing
    return 50.0; // 50ms for testing
}
