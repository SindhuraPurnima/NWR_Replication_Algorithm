#include "WorkStealler.h"
#include <vector>
#include <string>
#include <queue>
#include "proto/mini2.grpc.pb.h"

using namespace mini2;

std::vector<Message> WorkStealler::stealWork(const std::string& target_server, int count) {
    StealRequest request;
    request.set_source_server(server_id_);
    request.set_requested_items(count);

    StealResponse response;
    auto status = server_stubs_[target_server]->StealWork(&context, &request, &response);

    if (status.ok()) {
        std::vector<Message> stolen_messages;
        for (const auto& msg : response.stolen_messages()) {
            stolen_messages.push_back(msg);
        }
        return stolen_messages;
    }
    return std::vector<Message>();
}

void WorkStealler::handleStealRequest(const StealRequest* request, StealResponse* response) {
    auto messages_to_steal = replication_algorithm_.selectMessagesToSteal(
        message_queue_, 
        request->requested_items()
    );

    for (const auto& msg : messages_to_steal) {
        auto* stolen_msg = response->add_stolen_messages();
        *stolen_msg = msg;
    }
}