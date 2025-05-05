#include <grpcpp/grpcpp.h>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include "proto/mini2.grpc.pb.h"
#include <iostream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using mini2::InterServerService;
using mini2::CollisionData;
using mini2::CollisionBatch;
using mini2::Empty;
using mini2::DatasetInfo;
using namespace mini2;

class Client {
public:
    Client(const std::vector<std::string>& server_addresses)
        : server_addresses_(server_addresses) {
        // Create stubs for all servers
        for (const auto& address : server_addresses) {
            auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
            stubs_.push_back(InterServerService::NewStub(channel));
        }
    }

    void SendCollisionData(const CollisionData& collision) {
        // Create a batch with a single collision
        CollisionBatch batch;
        *batch.add_collisions() = collision;

        // Send to all servers
        for (size_t i = 0; i < stubs_.size(); ++i) {
            ClientContext context;
            Empty response;
            Status status = stubs_[i]->ForwardData(&context, batch, &response);
            
            if (!status.ok()) {
                std::cerr << "Failed to send data to server " << server_addresses_[i]
                          << ": " << status.error_message() << std::endl;
            }
        }
    }

    void SendCollisionBatch(const CollisionBatch& batch) {
        // Send to all servers
        for (size_t i = 0; i < stubs_.size(); ++i) {
            ClientContext context;
            Empty response;
            Status status = stubs_[i]->ForwardData(&context, batch, &response);
            
            if (!status.ok()) {
                std::cerr << "Failed to send batch to server " << server_addresses_[i]
                          << ": " << status.error_message() << std::endl;
            }
        }
    }

    void ShareAnalysis(const RiskAssessment& analysis) {
        // Send to all servers
        for (size_t i = 0; i < stubs_.size(); ++i) {
            ClientContext context;
            Empty response;
            Status status = stubs_[i]->ShareAnalysis(&context, analysis, &response);
            
            if (!status.ok()) {
                std::cerr << "Failed to share analysis with server " << server_addresses_[i]
                          << ": " << status.error_message() << std::endl;
            }
        }
    }

    void SetTotalDatasetSize(int64_t size) {
        DatasetInfo info;
        info.set_total_size(size);

        // Send to all servers
        for (size_t i = 0; i < stubs_.size(); ++i) {
            ClientContext context;
            Empty response;
            Status status = stubs_[i]->SetTotalDatasetSize(&context, info, &response);
            
            if (!status.ok()) {
                std::cerr << "Failed to set dataset size on server " << server_addresses_[i]
                          << ": " << status.error_message() << std::endl;
            }
        }
    }

    void ShareDatasetInfo(const std::string& dataset_id, int64_t size) {
        ClientContext context;
        DatasetInfo info;
        info.set_total_size(size);
        Empty response;
        
        Status status = stubs_[0]->SetTotalDatasetSize(&context, info, &response);
        if (!status.ok()) {
            std::cerr << "Failed to share dataset info: " << status.error_message() << std::endl;
        }
    }

private:
    std::vector<std::string> server_addresses_;
    std::vector<std::unique_ptr<InterServerService::Stub>> stubs_;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server_address1> [server_address2 ...]" << std::endl;
        return 1;
    }

    // We'll only use the first server to demonstrate work stealing
    std::vector<std::string> server_addresses;
    server_addresses.push_back(argv[1]);  // Only use the first server
    
    std::cout << "Sending all data to primary server: " << argv[1] << std::endl;
    std::cout << "The server will redistribute work based on load balancing" << std::endl;
    std::cout << "Using increased data volume (1000 records) and sending delay to facilitate work stealing" << std::endl;

    // Create client
    Client client(server_addresses);

    // Send multiple collision data packets
    std::cout << "Sending collision data to primary server..." << std::endl;
    
    // Create different boroughs for testing
    std::vector<std::string> boroughs = {"Manhattan", "Brooklyn", "Queens", "Bronx", "Staten Island"};
    
    // Send 1000 collision data points instead of 100
    const int TOTAL_RECORDS = 1000;
    const int DELAY_MS = 100;
    
    for (int i = 0; i < TOTAL_RECORDS; i++) {
        CollisionData collision;
        collision.set_collision_id("collision_" + std::to_string(i));
        collision.set_borough(boroughs[i % boroughs.size()]);
        collision.set_number_of_persons_injured(i % 5);
        collision.set_number_of_persons_killed(i % 2);
        
        // Create a batch with this collision
        CollisionBatch batch;
        *batch.add_collisions() = collision;
        
        // Send to primary server
        client.SendCollisionBatch(batch);
        
        // Add delay between sends to give servers time to redistribute
        std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_MS));
        
        // Show progress more frequently
        if (i % 50 == 0 || i == TOTAL_RECORDS - 1) {
            std::cout << "Sent " << i + 1 << " of " << TOTAL_RECORDS << " collision records (" 
                      << (i + 1) * 100 / TOTAL_RECORDS << "%)" << std::endl;
        }
    }
    
    std::cout << "Finished sending all collision data." << std::endl;
    return 0;
}

