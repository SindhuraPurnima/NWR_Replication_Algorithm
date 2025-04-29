#include "MetricsCollector.h"
#include <sys/sysinfo.h>
#include <thread>

double MetricsCollector::getCpuLoad() {
    static clock_t lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors;
    
    struct sysinfo myinfo;
    sysinfo(&myinfo);

    clock_t now = clock();
    double percent = ((now - lastCPU) / (double)CLOCKS_PER_SEC) * 100.0;
    lastCPU = now;
    
    return percent;
}

ServerMetrics MetricsCollector::collectMetrics() {
    ServerMetrics metrics;
    metrics.cpu_load = getCpuLoad();
    metrics.memory_usage = getMemoryUsage();
    metrics.queue_size = getCurrentQueueSize();
    metrics.network_latency = measureNetworkLatency();
    return metrics;
}

double MetricsCollector::getCpuUsage() {
    // Implement CPU usage collection
    // You can use platform-specific APIs or libraries
    return 0.0; // Placeholder
}

double MetricsCollector::measureNetworkLatency() {
    // Measure network latency to neighbors
    double total_latency = 0.0;
    int count = 0;
    
    for (const auto& [server_id, stub] : server_stubs_) {
        auto start = std::chrono::high_resolution_clock::now();
        MetricsUpdate metrics;
        auto status = stub->UpdateMetrics(&metrics);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (status.ok()) {
            total_latency += std::chrono::duration<double>(end - start).count();
            count++;
        }
    }
    
    return count > 0 ? total_latency / count : 0.0;
}
