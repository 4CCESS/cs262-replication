#include "client.h"
#include <iostream>
#include <sstream>
#include <functional>
#include <chrono>
#include <thread>

// Simple hash function for demonstration (DO NOT USE in production)
std::string ChatClient::HashPassword(const std::string& password) {
    // This is a very basic hash for demo purposes only
    std::hash<std::string> hasher;
    std::ostringstream ss;
    ss << "simplehash_" << hasher(password);
    return ss.str();
}

ChatClient::ChatClient(const std::string& server_address) {
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    stub_ = chat::ChatService::NewStub(channel);
}

ChatClient::~ChatClient() {
    StopMessageListener();
}

bool ChatClient::Register(const std::string& username, const std::string& password) {
    grpc::ClientContext context;
    chat::RegisterRequest request;
    chat::AuthResponse response;
    
    request.set_username(username);
    request.set_password(password);  // In a real app, hash the password on the client side
    
    grpc::Status status = stub_->Login(&context, request, &response);
    
    if (status.ok() && response.success()) {
        username_ = username;
        token_ = response.token();
        return true;
    } else {
        std::cerr << "Login failed: " 
                  << (response.success() ? status.error_message() : response.error_message()) 
                  << std::endl;
        return false;
    }
}

bool ChatClient::SendMessage(const std::string& content) {
    if (token_.empty()) {
        std::cerr << "Not logged in!" << std::endl;
        return false;
    }
    
    grpc::ClientContext context;
    chat::Message request;
    chat::SendResponse response;
    
    request.set_sender(username_);
    request.set_content(content);
    request.set_timestamp(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    request.set_token(token_);
    
    grpc::Status status = stub_->SendMessage(&context, request, &response);
    
    if (status.ok() && response.success()) {
        return true;
    } else {
        std::cerr << "Failed to send message: " 
                  << (response.success() ? status.error_message() : response.error_message()) 
                  << std::endl;
        return false;
    }
}

void ChatClient::StartMessageListener() {
    if (listening_ || token_.empty()) {
        return;
    }
    
    listening_ = true;
    
    listener_thread_ = std::thread([this]() {
        while (listening_) {
            // Create a request to get messages since last_message_time_
            grpc::ClientContext context;
            chat::GetMessagesRequest request;
            request.set_since_timestamp(last_message_time_);
            request.set_token(token_);
            
            // Use the streaming RPC to receive messages
            std::unique_ptr<grpc::ClientReader<chat::Message>> reader(
                stub_->GetMessages(&context, request));
            
            chat::Message message;
            while (reader->Read(&message)) {
                // Update the last message time
                if (message.timestamp() > last_message_time_) {
                    last_message_time_ = message.timestamp();
                }
                
                // Call the callback if set
                if (message_callback_) {
                    message_callback_(message.sender(), message.content());
                }
            }
            
            grpc::Status status = reader->Finish();
            if (!status.ok()) {
                std::cerr << "GetMessages RPC failed: " << status.error_message() << std::endl;
            }
            
            // Wait a bit before polling again
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

void ChatClient::StopMessageListener() {
    if (!listening_) {
        return;
    }
    
    listening_ = false;
    
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}
grpc::Status status = stub_->Register(&context, request, &response);
    
    if (status.ok() && response.success()) {
        username_ = username;
        token_ = response.token();
        return true;
    } else {
        std::cerr << "Registration failed: " 
                  << (response.success() ? status.error_message() : response.error_message()) 
                  << std::endl;
        return false;
    }
}

bool ChatClient::Login(const std::string& username, const std::string& password) {
    grpc::ClientContext context;
    chat::LoginRequest request;
    chat::AuthResponse response;
    
    request.set_username(username);
    request.set_password(password);  // In a real app, hash the password on the client side
    
    