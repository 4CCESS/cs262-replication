#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Secure authentication suite
#include "user_auth/user_auth.h"

// Convenience using declarations.
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerReaderWriter;

using chat::ChatService;
using chat::RegisterRequest;
using chat::RegisterResponse;
using chat::LoginRequest;
using chat::LoginResponse;
using chat::ChatMessage;
using chat::MessageResponse;
using chat::UndeliveredMessagesRequest;
using chat::UndeliveredMessagesResponse;
using chat::DeleteMessageRequest;
using chat::DeleteMessageResponse;
using chat::SearchUsersRequest;
using chat::SearchUsersResponse;

// Helper function to read an entire file into a string.
std::string ReadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "[ERROR] Unable to open file: " << filename << "\n";
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// In-memory data structures for messages and users.
struct Message {
    std::string id;
    std::string content;
    std::string sender;
    std::string recipient;
    bool delivered = false;
};

struct UserInfo {
    std::string password; // Hashed password
    bool isOnline = false;
    std::vector<Message> offlineMessages;
};

std::mutex user_mutex;
std::unordered_map<std::string, UserInfo> user_map;

std::mutex messages_mutex;
std::unordered_map<int, Message> messages;
int messageCounter = 0;

// Global mapping for active streams.
std::mutex stream_mutex;
std::unordered_map<std::string, ServerReaderWriter<ChatMessage, ChatMessage>*> activeStreams;

class ChatServiceImpl final : public ChatService::Service {
public:
    // Register a new user.
    Status Register(ServerContext* context, const RegisterRequest* request,
                    RegisterResponse* response) override {
        std::cout << "[DEBUG] Register called for username: " << request->username() << std::endl;
        std::lock_guard<std::mutex> lock(user_mutex);
        std::string username = request->username();
        if (user_map.find(username) != user_map.end()) {
            response->set_success(false);
            response->set_message("Username already exists.");
            std::cout << "[DEBUG] Registration failed: username already exists" << std::endl;
            return Status::OK;
        }
        UserInfo newUser;
        newUser.password = argon2HashPassword(request->password());
        newUser.isOnline = false;
        user_map[username] = newUser;
        response->set_success(true);
        response->set_message("User registered successfully.");
        std::cout << "[DEBUG] Registration successful for username: " << username << std::endl;
        return Status::OK;
    }
    
    // User login.
    Status Login(ServerContext* context, const LoginRequest* request,
                 LoginResponse* response) override {
        std::cout << "[DEBUG] Login called for username: " << request->username() << std::endl;
        std::lock_guard<std::mutex> lock(user_mutex);
        std::string username = request->username();
        auto it = user_map.find(username);
        if (it == user_map.end() || !argon2CheckPassword(it->second.password, request->password())) {
            response->set_success(false);
            response->set_message("Invalid username or password.");
            std::cout << "[DEBUG] Login failed for username: " << username << std::endl;
            return Status::OK;
        }
        it->second.isOnline = true;
        response->set_success(true);
        response->set_session_token("dummy_token");  // Replace with a secure token.
        response->set_message("Login successful.");
        std::cout << "[DEBUG] Login successful for username: " << username << std::endl;
        return Status::OK;
    }
    
    // Retrieve undelivered (offline) messages.
    Status RetrieveUndeliveredMessages(ServerContext* context, const UndeliveredMessagesRequest* request,
                                       UndeliveredMessagesResponse* response) override {
        std::cout << "[DEBUG] RetrieveUndeliveredMessages called for user: " << request->username() << std::endl;
        std::lock_guard<std::mutex> lock(user_mutex);
        auto it = user_map.find(request->username());
        if (it == user_map.end()) {
            std::cout << "[DEBUG] User not found: " << request->username() << std::endl;
            return Status(grpc::NOT_FOUND, "User not found.");
        }
        int max_messages = request->max_messages();
        int count = 0;
        for (const auto& msg : it->second.offlineMessages) {
            ChatMessage* chat_msg = response->add_messages();
            chat_msg->set_sender(msg.sender);
            chat_msg->set_recipient(msg.recipient);
            chat_msg->set_message(msg.content);
            chat_msg->set_timestamp("");
            count++;
            if (count >= max_messages)
                break;
        }
        if (count > 0) {
            it->second.offlineMessages.erase(it->second.offlineMessages.begin(),
                                             it->second.offlineMessages.begin() + count);
        }
        std::cout << "[DEBUG] Delivered " << count << " offline messages to user: " << request->username() << std::endl;
        return Status::OK;
    }
    
    // Delete a message.
    Status DeleteMessage(ServerContext* context, const DeleteMessageRequest* request,
                         DeleteMessageResponse* response) override {
        std::cout << "[DEBUG] DeleteMessage called for message_id: " << request->message_id() 
                  << " by user: " << request->requesting_user() << std::endl;
        int msg_id = std::stoi(request->message_id());
        std::lock_guard<std::mutex> lock(messages_mutex);
        auto it = messages.find(msg_id);
        if (it == messages.end()) {
            response->set_success(false);
            std::cout << "[DEBUG] Message not found: " << msg_id << std::endl;
            return Status::OK;
        }
        if (it->second.recipient != request->requesting_user()) {
            response->set_success(false);
            std::cout << "[DEBUG] Unauthorized delete attempt by user: " << request->requesting_user() << std::endl;
            return Status::OK;
        }
        messages.erase(it);
        response->set_success(true);
        std::cout << "[DEBUG] Successfully deleted message: " << msg_id << std::endl;
        return Status::OK;
    }
    
    // Bidirectional streaming for real-time chat.
    Status ChatStream(ServerContext* context,
                      ServerReaderWriter<ChatMessage, ChatMessage>* stream) override {
        std::cout << "[DEBUG] ChatStream: New stream connection received." << std::endl;
        // Read the initial message to determine the user's identity.
        ChatMessage init_msg;
        if (!stream->Read(&init_msg)) {
            std::cerr << "[DEBUG] ChatStream: Failed to read init message." << std::endl;
            return Status::OK;
        }
        std::string username = init_msg.sender();
        std::cout << "[DEBUG] ChatStream: Received init message from user: " << username << std::endl;
        
        // Register the stream.
        {
            std::lock_guard<std::mutex> lock(stream_mutex);
            activeStreams[username] = stream;
        }

        // Send message history.
        std::vector<std::pair<int, Message>> history;
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            for (const auto &kv : messages) {
                const Message &msg = kv.second;
                if (msg.delivered && (msg.sender == username || msg.recipient == username)) {
                    history.push_back({kv.first, msg});
                }
            }
        }
        std::sort(history.begin(), history.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        std::cout << "[DEBUG] ChatStream: Sending " << history.size() 
                  << " historical messages to user: " << username << std::endl;
        for (const auto &entry : history) {
            ChatMessage history_msg;
            history_msg.set_sender(entry.second.sender);
            history_msg.set_recipient(entry.second.recipient);
            history_msg.set_message(entry.second.content);
            history_msg.set_message_id(entry.second.id);
            if (!stream->Write(history_msg)) {
                std::cerr << "[WARN] ChatStream: Failed to send history message for user " << username << std::endl;
                break;
            }
        }
        
        // Process incoming messages.
        ChatMessage incoming;
        while (stream->Read(&incoming)) {
            std::string recipient = incoming.recipient();
            std::string ack_value = "-1"; // default failure ack
            {
                std::lock_guard<std::mutex> userLock(user_mutex);
                auto it1 = user_map.find(recipient);
                if (it1 != user_map.end()) {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    messageCounter++;
                    std::string msg_id = std::to_string(messageCounter);
                    Message msg;
                    msg.id = msg_id;
                    msg.content = incoming.message();
                    msg.sender = incoming.sender();
                    msg.recipient = incoming.recipient();
                    messages[messageCounter] = msg;
                    {
                        std::lock_guard<std::mutex> lock(stream_mutex);
                        auto it2 = activeStreams.find(recipient);
                        if (it2 != activeStreams.end()){
                            incoming.set_message_id(msg_id);
                            msg.delivered = true;
                            it2->second->Write(incoming);
                            std::cout << "[DEBUG] ChatStream: Delivered message " << msg_id 
                                      << " from " << incoming.sender() << " to " << recipient << std::endl;
                        } else {
                            it1->second.offlineMessages.push_back(msg);
                            std::cout << "[DEBUG] ChatStream: Queued message " << msg_id 
                                      << " for offline user " << recipient << std::endl;
                        }
                    }
                    ack_value = msg_id;
                } else {
                    std::cout << "[WARN] ChatStream: Recipient " << recipient << " not found." << std::endl;
                }
            }
            // Send ACK back to the sender.
            ChatMessage ackMsg;
            ackMsg.set_sender("server");
            ackMsg.set_recipient(username);
            ackMsg.set_message("ACK");
            ackMsg.set_message_id(ack_value);
            if (!stream->Write(ackMsg)) {
                std::cerr << "[WARN] ChatStream: Failed to send ACK for user " << username << std::endl;
            }
            std::cout << "[DEBUG] ChatStream: Processed incoming message from " << incoming.sender()
                      << " to " << recipient << " with ACK " << ack_value << std::endl;
            std::cout << "[MSGSIZE] Acknowledgement: " << ackMsg.ByteSizeLong() 
                      << " bytes" << std::endl;
        }
        
        {
            std::lock_guard<std::mutex> lock(stream_mutex);
            activeStreams.erase(username);
        }
        std::cout << "[DEBUG] ChatStream: Stream closed for user " << username << std::endl;
        return Status::OK;
    }
    
    Status SearchUsers(ServerContext* context, const SearchUsersRequest* request,
                       SearchUsersResponse* response) override {
        std::string pattern = request->wildcard();
        std::cout << "[DEBUG] SearchUsers: Searching for pattern: " << pattern << std::endl;
        std::lock_guard<std::mutex> lock(user_mutex);
        for (const auto& kv : user_map) {
            if (kv.first.find(pattern) != std::string::npos) {
                response->add_usernames(kv.first);
            }
        }
        std::cout << "[DEBUG] SearchUsers: Found " << response->usernames_size() 
                  << " users matching pattern." << std::endl;
        return Status::OK;
    }
};

void RunServer(const std::string& server_address) {
    ChatServiceImpl service;
    // Configure TLS credentials.
    grpc::SslServerCredentialsOptions ssl_opts;
    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
    pkcp.cert_chain = ReadFile("server.crt");
    pkcp.private_key = ReadFile("server.key");
    ssl_opts.pem_key_cert_pairs.push_back(pkcp);
    // For mutual TLS, set ssl_opts.pem_root_certs = ReadFile("ca.crt");

    // Using insecure credentials for debugging TLS issues.
    std::shared_ptr<grpc::ServerCredentials> creds = grpc::InsecureServerCredentials();
    // For TLS, switch to:
    // std::shared_ptr<grpc::ServerCredentials> creds = grpc::SslServerCredentials(ssl_opts);

    ServerBuilder builder;
    builder.AddListeningPort(server_address, creds);
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[INFO] Server listening on " << server_address << "\n";
    server->Wait();
}

int main(int argc, char** argv) {
    std::string server_address("127.0.0.1:5000");
    RunServer(server_address);
    return 0;
}
