#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <grpcpp/grpcpp.h>
#include "proto/mini2.grpc.pb.h"

using namespace mini2;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

class ServerMonitor {
public:
    ServerMonitor(const std::string& server_address) 
        : stub_(InterServerService::NewStub(
            grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()))),
          server_address_(server_address) {}
    
    bool CheckHealth(const std::string& server_id) {
        ServerInfo request;
        request.set_server_id(server_id);
        ServerInfo response;
        ClientContext context;
        
        // Set a deadline for RPC
        std::chrono::system_clock::time_point deadline = 
            std::chrono::system_clock::now() + std::chrono::milliseconds(500);
        context.set_deadline(deadline);
        
        Status status = stub_->HealthCheck(&context, request, &response);
        
        if (status.ok()) {
            // Cache the server info for later use
            cached_server_info_ = response;
            return true;
        }
        return false;
    }
    
    bool GetMetrics(const std::string& server_id, ServerMetrics& metrics) {
        // If we already have metrics from health check, use those
        if (!cached_server_info_.server_id().empty() && 
            cached_server_info_.server_id() == server_id) {
            metrics = cached_server_info_.metrics();
            return true;
        }
        
        // Otherwise try to get metrics via UpdateMetrics call
        MetricsUpdate request;
        request.set_server_id(server_id);
        auto* req_metrics = request.mutable_metrics();
        req_metrics->set_cpu_utilization(0.0);
        req_metrics->set_memory_usage(0.0);
        req_metrics->set_message_queue_length(0);
        req_metrics->set_avg_network_latency(0.0);
        
        MetricsResponse response;
        ClientContext context;
        
        Status status = stub_->UpdateMetrics(&context, request, &response);
        if (status.ok() && response.success()) {
            // Try to get the metrics from health check again
            if (CheckHealth(server_id)) {
                metrics = cached_server_info_.metrics();
                return true;
            }
        }
        return false;
    }
    
    std::string GetServerAddress() const {
        return server_address_;
    }
    
private:
    std::unique_ptr<InterServerService::Stub> stub_;
    std::string server_address_;
    ServerInfo cached_server_info_;
};

double calculateRank(const ServerMetrics& metrics) {
    // Use the same ranking algorithm as the server
    double queue_weight = 0.3;
    double cpu_weight = 0.2;
    double memory_weight = 0.2;
    double latency_weight = 0.2;
    double distance_weight = 0.1;
    
    // Normalize queue size
    double queue_norm = metrics.message_queue_length();
    if (metrics.max_queue_length() > 0) {
        queue_norm /= metrics.max_queue_length();
    }
    
    // Normalize latency (assuming 100ms as max acceptable)
    double latency_norm = metrics.avg_network_latency() / 100.0;
    
    // Calculate rank
    return (queue_weight * queue_norm) + 
           (cpu_weight * metrics.cpu_utilization()) + 
           (memory_weight * metrics.memory_usage()) + 
           (latency_weight * latency_norm) + 
           (distance_weight * 0.5); // Assume middle distance
}

void printServerMetrics(const std::string& server_id, const ServerMetrics& metrics) {
    double rank = calculateRank(metrics);
    
    std::cout << "Server: " << server_id << "\n";
    std::cout << "  CPU: " << std::fixed << std::setprecision(2) 
              << (metrics.cpu_utilization() * 100) << "%\n";
    std::cout << "  Memory: " << std::fixed << std::setprecision(2) 
              << (metrics.memory_usage() * 100) << "%\n";
    std::cout << "  Queue: " << metrics.message_queue_length() 
              << "/" << metrics.max_queue_length() << "\n";
    std::cout << "  Latency: " << std::fixed << std::setprecision(2) 
              << metrics.avg_network_latency() << "ms\n";
    std::cout << "  Rank: " << std::fixed << std::setprecision(3) << rank << "\n";
}

int main(int argc, char** argv) {
    std::cout << "Server Load Monitoring Tool\n";
    std::cout << "==========================\n\n";
    
    // Default server addresses (5 servers as per requirements)
    std::vector<std::string> server_addresses = {
        "localhost:50051",
        "localhost:50052",
        "localhost:50053",
        "localhost:50054",
        "localhost:50055"
    };
    
    // Check if server addresses are provided as args
    if (argc > 1) {
        server_addresses.clear();
        for (int i = 1; i < argc; i++) {
            server_addresses.push_back(argv[i]);
        }
    }
    
    // Create monitors for each server
    std::vector<ServerMonitor> monitors;
    for (const auto& address : server_addresses) {
        monitors.emplace_back(address);
    }
    
    // Print header
    std::cout << "Monitoring " << server_addresses.size() << " servers:\n";
    for (const auto& address : server_addresses) {
        std::cout << "  - " << address << "\n";
    }
    std::cout << "\n";
    
    // Start monitoring loop
    while (true) {
        // Store the time_t value in a variable first
        auto now = std::chrono::system_clock::now();
        time_t time_now = std::chrono::system_clock::to_time_t(now);
        
        std::cout << "\n======== SERVER STATUS (" 
                 << std::put_time(std::localtime(&time_now), "%H:%M:%S")
                 << ") ========\n\n";
        
        for (size_t i = 0; i < server_addresses.size(); i++) {
            std::string server_id = "server" + std::to_string(i+1);
            bool is_alive = monitors[i].CheckHealth(server_id);
            
            if (is_alive) {
                ServerMetrics metrics;
                if (monitors[i].GetMetrics(server_id, metrics)) {
                    std::cout << "[ONLINE] ";
                    printServerMetrics(server_id, metrics);
                } else {
                    std::cout << "[ONLINE] Server " << server_id 
                             << " at " << monitors[i].GetServerAddress() 
                             << " - metrics not available\n";
                }
            } else {
                std::cout << "[OFFLINE] Server " << server_id 
                         << " at " << monitors[i].GetServerAddress() << " is not responding\n";
            }
            std::cout << "\n";
        }
        
        // Wait 5 seconds before next update
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    return 0;
} 