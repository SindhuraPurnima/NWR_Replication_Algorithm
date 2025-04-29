class WorkStealer {
public:
    std::vector<Message> stealWork(const std::string& target_server, int count) {
        StealRequest request;
        request.set_source_server(server_id_);
        request.set_requested_items(count);
        
        StealResponse response;
        auto status = server_stubs_[target_server]->StealWork(&request, &response);
        
        if (status.ok()) {
            return std::vector<Message>(
                response.stolen_messages().begin(),
                response.stolen_messages().end()
            );
        }
        return {};
    }

    void handleStealRequest(const StealRequest* request, StealResponse* response) {
        auto messages_to_steal = replication_algorithm_.selectMessagesToSteal(
            message_queue_, 
            request->requested_items()
        );
        
        *response->mutable_stolen_messages() = {
            messages_to_steal.begin(),
            messages_to_steal.end()
        };
        
        // Remove stolen messages from our queue
        removeMessages(messages_to_steal);
    }
};