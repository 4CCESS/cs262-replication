#ifndef CHAT_CLIENT_H
#define CHAT_CLIENT_H

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"

class ChatClient {
public:
    ChatClient(const std::string& server_address);
    ~ChatClient();

    // User authentication
    bool Register(const std::string& username, const std::string& password);
    bool Login(const std::string& username, const std::string& password);
    bool IsLoggedIn() const { return !token_.empty(); }
    
    // Chat operations
    bool SendMessage(const std::string& content);
    void StartMessageListener();
    void StopMessageListener();
    
    // Callbacks
    void SetMessageCallback(std::function<void(const std::string&, const std::string&)> callback) {
        message_callback_ = callback;
    }

private:
    std::unique_ptr<chat::ChatService::Stub> stub_;
    std::string username_;
    std::string token_;
    int64_t last_message_time_ = 0;
    
    std::thread listener_thread_;
    std::atomic<bool> listening_ = {false};
    
    std::function<void(const std::string&, const std::string&)> message_callback_;
    
    // Simple password hashing (in a real app, use a proper hashing library)
    std::string HashPassword(const std::string& password);
};

#endif // CHAT_CLIENT_H
