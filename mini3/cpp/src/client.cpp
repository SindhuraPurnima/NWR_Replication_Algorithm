#include <grpcpp/grpcpp.h>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include "proto/mini2.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using mini2::EntryPointService;
using mini2::CollisionData;
using mini2::Empty;
using mini2::DatasetInfo;

class Client {
public:
    Client(const std::string& server_address) {
        auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
        stub_ = EntryPointService::NewStub(channel);
    }

    void processCSVFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return;
        }

        std::string line;
        
        // Skip header line
        std::getline(file, line);
        
        while (std::getline(file, line)) {
            auto collision = parseCSVLine(line);
            collisions_data_.push_back(collision);
        }

        // Send dataset size info
        ClientContext context;
        DatasetInfo info;
        info.set_total_size(collisions_data_.size());
        Empty response;
        Status status = stub_->SetDatasetInfo(&context, info, &response);
        
        if (!status.ok()) {
            std::cerr << "Failed to send dataset size info: " << status.error_message() << std::endl;
        }
    }

    void sendCollisions() {
        ClientContext context;
        Empty response;
        
        // Create a writer for streaming collisions
        std::unique_ptr<grpc::ClientWriter<CollisionData>> writer = stub_->StreamCollisions(&context, &response);
        
        for (const auto& collision : collisions_data_) {
            if (!writer->Write(collision)) {
                std::cerr << "Failed to write collision data" << std::endl;
                break;
            }
        }
        
        // Finish writing
        writer->WritesDone();
        grpc::Status status = writer->Finish();
        
        if (!status.ok()) {
            std::cerr << "StreamCollisions rpc failed: " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<EntryPointService::Stub> stub_;
    std::vector<CollisionData> collisions_data_;

    CollisionData parseCSVLine(const std::string& line) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;
        
        // Handle quoted fields and commas within quotes
        bool in_quotes = false;
        std::string current_token;
        
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                tokens.push_back(current_token);
                current_token.clear();
            } else {
                current_token += c;
            }
        }
        tokens.push_back(current_token);  // Add the last token
        
        CollisionData collision;
        // Set fields according to the protobuf definition
        if (tokens.size() >= 29) {  // Make sure we have enough tokens
            collision.set_crash_date(tokens[0]);
            collision.set_crash_time(tokens[1]);
            collision.set_borough(tokens[2]);
            collision.set_zip_code(tokens[3]);
            
            // Handle latitude and longitude - convert from string to double
            try {
                if (!tokens[4].empty()) collision.set_latitude(std::stod(tokens[4]));
                if (!tokens[5].empty()) collision.set_longitude(std::stod(tokens[5]));
            } catch (const std::exception& e) {
                std::cerr << "Error converting lat/long: " << e.what() << std::endl;
            }
            
            collision.set_location(tokens[6]);
            collision.set_on_street_name(tokens[7]);
            collision.set_cross_street_name(tokens[8]);
            collision.set_off_street_name(tokens[9]);
            
            // Handle numeric fields - convert from string to int32
            try {
                if (!tokens[10].empty()) collision.set_number_of_persons_injured(std::stoi(tokens[10]));
                if (!tokens[11].empty()) collision.set_number_of_persons_killed(std::stoi(tokens[11]));
                if (!tokens[12].empty()) collision.set_number_of_pedestrians_injured(std::stoi(tokens[12]));
                if (!tokens[13].empty()) collision.set_number_of_pedestrians_killed(std::stoi(tokens[13]));
                if (!tokens[14].empty()) collision.set_number_of_cyclist_injured(std::stoi(tokens[14]));
                if (!tokens[15].empty()) collision.set_number_of_cyclist_killed(std::stoi(tokens[15]));
                if (!tokens[16].empty()) collision.set_number_of_motorist_injured(std::stoi(tokens[16]));
                if (!tokens[17].empty()) collision.set_number_of_motorist_killed(std::stoi(tokens[17]));
            } catch (const std::exception& e) {
                std::cerr << "Error converting numeric fields: " << e.what() << std::endl;
            }
            
            collision.set_contributing_factor_vehicle_1(tokens[18]);
            collision.set_contributing_factor_vehicle_2(tokens[19]);
            collision.set_contributing_factor_vehicle_3(tokens[20]);
            collision.set_contributing_factor_vehicle_4(tokens[21]);
            collision.set_contributing_factor_vehicle_5(tokens[22]);
            collision.set_collision_id(tokens[23]);
            collision.set_vehicle_type_code_1(tokens[24]);
            collision.set_vehicle_type_code_2(tokens[25]);
            collision.set_vehicle_type_code_3(tokens[26]);
            collision.set_vehicle_type_code_4(tokens[27]);
            collision.set_vehicle_type_code_5(tokens[28]);
        }
        
        return collision;
    }
};

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_address> <csv_file>" << std::endl;
        return 1;
    }

    std::string server_address = argv[1];
    std::string csv_file = argv[2];

    Client client(server_address);
    client.processCSVFile(csv_file);
    client.sendCollisions();

    return 0;
}

