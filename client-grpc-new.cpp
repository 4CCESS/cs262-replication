#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"

#include <iostream>
#include <sstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <functional>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;

// Using declarations for ChatService and DeliveryService messages and RPCs.
using chat::ChatService;
using chat::RegisterRequest;
using chat::RegisterResponse;
using chat::LoginRequest;
using chat::LoginResponse;
using chat::ChatMessage;
using chat::UndeliveredMessagesRequest;
using chat::UndeliveredMessagesResponse;
using chat::DeleteMessageRequest;
using chat::DeleteMessageResponse;
using chat::SearchUsersRequest;
using chat::SearchUsersResponse;
using chat::LeaderRequest;
using chat::LeaderResponse;
using chat::MessageResponse;

using chat::DeliveryService;
using chat::DeliveryRequest;
using chat::DeliveryResponse;

// Global stub for ChatService.
std::unique_ptr<ChatService::Stub> g_stub;
std::atomic<bool> g_keep_running(true);
std::string g_current_user;
std::mutex g_cout_mutex;

// Global variable for this client's delivery address.
std::string g_delivery_address;

// ------------------ Helper Functions ------------------

// Thread-safe JSON output (for GUI/logging).
void PrintJson(const std::string &json_str) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << json_str << std::endl;
}

// Thread-safe debug logging.
void PrintClientDebug(const std::string &msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << "[CLIENT] " << msg << std::endl;
}

// Helper to build a JSON string from fields.
std::string JsonString(const std::string &type, const std::string &sender,
                       const std::string &message_id, const std::string &recipient,
                       const std::string &content) {
    std::ostringstream oss;
    oss << "{\"type\":\"" << type << "\","
        << "\"sender\":\"" << sender << "\","
        << "\"message_id\":\"" << message_id << "\","
        << "\"recipient\":\"" << recipient << "\","
        << "\"message\":\"" << content << "\"}";
    return oss.str();
}

// Load server addresses from a file.
std::vector<std::string> loadServerAddresses(const std::string &filename) {
    std::vector<std::string> addresses;
    std::ifstream file(filename);
    if (!file.is_open()){
        std::cerr << "[CLIENT] Unable to open " << filename << "\n";
        return addresses;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty())
            addresses.push_back(line);
    }
    return addresses;
}

// Attempts to determine the primary server by calling GetLeaderInfo.
std::string getPrimaryAddress(const std::vector<std::string> &serverList) {
    for (const auto &addr : serverList) {
        PrintClientDebug("Attempting connection to server: " + addr);
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        std::unique_ptr<ChatService::Stub> stub = ChatService::NewStub(channel);
        LeaderRequest req;
        req.set_client_id("client_dummy");
        LeaderResponse resp;
        ClientContext context;
        Status status = stub->GetLeaderInfo(&context, req, &resp);
        if (status.ok() && !resp.leader_address().empty()) {
            PrintClientDebug("Primary determined as: " + resp.leader_address());
            return resp.leader_address();
        }
    }
    return "";
}

// Updates the global ChatService stub to point to the new primary.
void updateStubToPrimary(const std::string &primaryAddress) {
    PrintClientDebug("Updating primary stub to: " + primaryAddress);
    auto channel = grpc::CreateChannel(primaryAddress, grpc::InsecureChannelCredentials());
    g_stub = ChatService::NewStub(channel);
}

// ------------------ Operation Helper Functions ------------------

/*
 * performLogin - Attempts to log in the user.
 * Tries up to 3 times. If all attempts fail, updates the primary stub and reattempts the same command.
 */
bool performLogin(const std::string &username, const std::string &password) {
    const int maxAttempts = 3;
    int attempts = 0;
    while (attempts < maxAttempts) {
        attempts++;
        PrintClientDebug("Sending Login RPC for user: " + username + " (Attempt " + std::to_string(attempts) + ")");
        LoginRequest req;
        req.set_username(username);
        req.set_password(password);
        req.set_delivery_address(g_delivery_address);
        LoginResponse resp;
        ClientContext context;
        Status status = g_stub->Login(&context, req, &resp);
        if (status.ok()) {
            if (resp.success()){
                g_current_user = username;
                // Create a JSON object with type "validation" and the login message.
                PrintJson("{\"type\":\"validation\", \"message\":\"Login successful.\"}");
                // If the login response includes message history, forward it as a separate "history" message.
                if (resp.messages_size() > 0) {
                    // Build a JSON array of messages.
                    std::string historyJson = "{\"type\":\"history\", \"messages\":[";
                    for (int i = 0; i < resp.messages_size(); i++) {
                        const ChatMessage &msg = resp.messages(i);
                        historyJson += "{\"sender\":\"" + msg.sender() +
                                       "\", \"recipient\":\"" + msg.recipient() +
                                       "\", \"message\":\"" + msg.message() +
                                       "\", \"message_id\":\"" + msg.message_id() + "\"}";
                        if (i < resp.messages_size() - 1)
                            historyJson += ",";
                    }
                    historyJson += "]}";
                    PrintJson(historyJson);
                }
            } else {
                PrintJson("{\"type\":\"validation\", \"message\":\"" + resp.message() + "\"}");
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

/*
 * performRegistration - Attempts to register a new user.
 * Retries up to 3 times. If all attempts fail, updates the primary and reattempts the same command.
 */
bool performRegistration(const std::string &username, const std::string &password) {
    const int maxAttempts = 3;
    int attempts = 0;
    while (attempts < maxAttempts) {
        attempts++;
        PrintClientDebug("Sending Register RPC for user: " + username + " (Attempt " + std::to_string(attempts) + ")");
        RegisterRequest req;
        req.set_username(username);
        req.set_password(password);
        RegisterResponse resp;
        ClientContext context;
        Status status = g_stub->Register(&context, req, &resp);
        if (status.ok()) {
            PrintJson("{\"type\":\"validation\", \"message\":\"" + resp.message() + "\"}");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

/*
 * performSendMessage - Attempts to send a chat message using the SendMessage RPC.
 * Retries up to 3 times. If all attempts fail, updates the primary stub and reattempts the same command.
 */
bool performSendMessage(const std::string &sender, const std::string &recipient, const std::string &message) {
    const int maxAttempts = 3;
    int attempts = 0;
    bool success = false;
    while (attempts < maxAttempts && !success) {
        attempts++;
        PrintClientDebug("Sending message from " + sender + " to " + recipient + " (Attempt " + std::to_string(attempts) + ")");
        ChatMessage msg;
        msg.set_sender(sender);
        msg.set_recipient(recipient);
        msg.set_message(message);
        // Timestamp and message_id are generated by the server.
        MessageResponse resp;
        ClientContext context;
        Status status = g_stub->SendMessage(&context, msg, &resp);
        if (status.ok()) {
            if (resp.success()){
                PrintJson("{\"type\":\"confirmation\", \"message\":\"Message sent: " + resp.message() + "\", \"message_id\":\"" + resp.message_id() + "\"}");
                success = true;
            } else {
                PrintJson("{\"type\":\"confirmation\", \"message\":\"Replication backups failed.\"}");
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

/*
 * performRetrieveMessages - Attempts to retrieve offline messages.
 * Retries up to 3 times; if unsuccessful, updates primary and reattempts.
 */
bool performRetrieveMessages(const std::string &username, int maxMessages) {
    const int maxAttempts = 3;
    int attempts = 0;
    while (attempts < maxAttempts) {
        attempts++;
        PrintClientDebug("Retrieving offline messages for user: " + username + " (Attempt " + std::to_string(attempts) + ")");
        UndeliveredMessagesRequest req;
        req.set_username(username);
        req.set_max_messages(maxMessages);
        UndeliveredMessagesResponse resp;
        ClientContext context;
        Status status = g_stub->RetrieveUndeliveredMessages(&context, req, &resp);
        if (status.ok()) {
            for (int i = 0; i < resp.messages_size(); i++) {
                const ChatMessage &msg = resp.messages(i);
                std::string json = JsonString("chat", msg.sender(), msg.message_id(), msg.recipient(), msg.message());
                PrintClientDebug(json);
                PrintJson(json);
            }
            PrintClientDebug("Retrieved " + std::to_string(resp.messages_size()) + " offline messages.");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

/*
 * performListUsers - Attempts to list users matching a given pattern.
 * Retries up to 3 times; if unsuccessful, updates primary and reattempts.
 */
bool performListUsers(const std::string &pattern) {
    const int maxAttempts = 3;
    int attempts = 0;
    while (attempts < maxAttempts) {
        attempts++;
        PrintClientDebug("Listing users matching pattern: " + pattern + " (Attempt " + std::to_string(attempts) + ")");
        SearchUsersRequest req;
        req.set_wildcard(pattern);
        SearchUsersResponse resp;
        ClientContext context;
        Status status = g_stub->SearchUsers(&context, req, &resp);
        if (status.ok()) {
            std::ostringstream oss;
            bool first = true;
            for (const auto &uname : resp.usernames()) {
                if (!first)
                    oss << ", ";
                oss << uname;
                first = false;
            }
            PrintJson("{\"type\":\"user_list\", \"message\":\"" + oss.str() + "\"}");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

/*
 * performDeleteMessage - Attempts to delete a specified message.
 * Retries up to 3 times; if unsuccessful, updates primary and reattempts.
 */
bool performDeleteMessage(const std::string &username, const std::string &message_id) {
    const int maxAttempts = 3;
    int attempts = 0;
    while (attempts < maxAttempts) {
        attempts++;
        PrintClientDebug("Deleting message " + message_id + " for user: " + username + " (Attempt " + std::to_string(attempts) + ")");
        DeleteMessageRequest req;
        req.set_message_id(message_id);
        req.set_requesting_user(username);
        DeleteMessageResponse resp;
        ClientContext context;
        Status status = g_stub->DeleteMessage(&context, req, &resp);
        if (status.ok() && resp.success()) {
            PrintJson("{\"type\":\"delete\", \"message_id\":\"[CLIENT] " + message_id + "\"}");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return false;
}

// ------------------ Delivery Service Implementation ------------------

/*
 * DeliveryServiceImpl - Implements the DeliveryService.
 * When the primary server delivers a message via DeliverMessage, this service prints the delivered message
 * and returns a DeliveryResponse indicating success.
 */
class DeliveryServiceImpl final : public DeliveryService::Service {
public:
    Status DeliverMessage(ServerContext* context, const DeliveryRequest* request,
                          DeliveryResponse* response) override {
        // Print delivered message details.
        PrintClientDebug("DeliveryService received message from: " + request->sender() +
                         " | message: " + request->message());
        std::ostringstream oss;
        oss << "{\"type\":\"chat\","
            << "\"sender\":\"" << request->sender() << "\","
            << "\"recipient\":\"" << request->recipient() << "\","
            << "\"message\":\"" << request->message() << "\","
            << "\"timestamp\":\"" << request->timestamp() << "\","
            << "\"message_id\":\"" << request->message_id() << "\"}";
        std::string json_str = oss.str();
        PrintJson(json_str);
        response->set_success(true);
        response->set_message("Delivery acknowledged.");
        return Status::OK;
    }
};

/*
 * RunDeliveryService - Starts a gRPC server to run the DeliveryService.
 * This allows the primary server to deliver messages to this client.
 */
void RunDeliveryService(const std::string &deliveryAddress) {
    DeliveryServiceImpl service;
    ServerBuilder builder;
    // Listen on the provided delivery address.
    builder.AddListeningPort(deliveryAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    PrintClientDebug("DeliveryService listening on " + deliveryAddress);
    server->Wait();
}

// ------------------ Main Client Loop ------------------

/*
 * RunClient - Main input loop.
 * For each command, if the operation fails 3 times, update the primary stub and reattempt the same command.
 */
void RunClient(const std::string &server_address) {
    PrintClientDebug("Creating channel to primary " + server_address);
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    g_stub = ChatService::NewStub(channel);
    
    // Use comma as a delimiter (change back to \x1F for GUI if needed).
    const char DELIMITER = ',';
    std::string line;
    PrintClientDebug("Starting main input loop...");
    while (g_keep_running.load() && std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        PrintClientDebug("Raw input: " + line);
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (std::getline(iss, token, DELIMITER))
            tokens.push_back(token);
        if (tokens.empty())
            continue;
        char op = tokens[0][0];
        PrintClientDebug("Parsed operation: " + std::string(1, op));
        
        // For each operation, if 3 attempts fail, re-discover primary and reattempt the same command.
        switch (op) {
            case 'L': { // Login
                if (tokens.size() < 3) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid login command.\"}");
                    break;
                }
                std::string username = tokens[1];
                std::string password = tokens[2];
                int overallRetries = 0;
                bool success = false;
                while (!success && overallRetries < 2) {
                    if (performLogin(username, password)) {
                        success = true;
                    } else {
                        overallRetries++;
                        PrintClientDebug("Login failed after 3 attempts. Re-discovering primary and retrying login.");
                        std::vector<std::string> serverList = loadServerAddresses("servers.txt");
                        std::string newPrimary = getPrimaryAddress(serverList);
                        if (!newPrimary.empty())
                            updateStubToPrimary(newPrimary);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                if (!success) {
                    PrintJson("{\"type\":\"validation\", \"message\":\"Login failed, please try again.\"}");
                }
                break;
            }
            case 'R': { // Registration
                if (tokens.size() < 3) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid register command.\"}");
                    break;
                }
                std::string username = tokens[1];
                std::string password = tokens[2];
                int overallRetries = 0;
                bool success = false;
                while (!success && overallRetries < 2) {
                    if (performRegistration(username, password)) {
                        success = true;
                    } else {
                        overallRetries++;
                        PrintClientDebug("Registration failed after 3 attempts. Re-discovering primary and retrying registration.");
                        std::vector<std::string> serverList = loadServerAddresses("servers.txt");
                        std::string newPrimary = getPrimaryAddress(serverList);
                        if (!newPrimary.empty())
                            updateStubToPrimary(newPrimary);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                if (!success) {
                    PrintJson("{\"type\":\"validation\", \"message\":\"Registration failed.\"}");
                }
                break;
            }
            case 's': { // Send message
                if (tokens.size() < 4) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid send message command.\"}");
                    break;
                }
                std::string sender = tokens[1];
                std::string recipient = tokens[2];
                std::string message = tokens[3];
                int overallRetries = 0;
                bool success = false;
                while (!success && overallRetries < 2) {
                    if (performSendMessage(sender, recipient, message)) {
                        success = true;
                    } else {
                        overallRetries++;
                        PrintClientDebug("Send message failed after 3 attempts. Re-discovering primary and retrying send.");
                        std::vector<std::string> serverList = loadServerAddresses("servers.txt");
                        std::string newPrimary = getPrimaryAddress(serverList);
                        if (!newPrimary.empty())
                            updateStubToPrimary(newPrimary);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                if (!success) {
                    PrintJson("{\"type\":\"error\", \"message\":\"Message send failed, please try again.\"}");
                }
                break;
            }
            case 'r': { // Retrieve offline messages
                if (tokens.size() < 3) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid retrieval command.\"}");
                    break;
                }
                std::string username = tokens[1];
                int max_messages = std::stoi(tokens[2]);
                int overallRetries = 0;
                bool success = false;
                while (!success && overallRetries < 2) {
                    if (performRetrieveMessages(username, max_messages)) {
                        success = true;
                    } else {
                        overallRetries++;
                        PrintClientDebug("Retrieve messages failed after 3 attempts. Re-discovering primary and retrying retrieval.");
                        std::vector<std::string> serverList = loadServerAddresses("servers.txt");
                        std::string newPrimary = getPrimaryAddress(serverList);
                        if (!newPrimary.empty())
                            updateStubToPrimary(newPrimary);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                if (!success) {
                    PrintJson("{\"type\":\"error\", \"message\":\"Failed to retrieve offline messages.\"}");
                }
                break;
            }
            case 'l': { // List users
                if (tokens.size() < 2) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid list users command.\"}");
                    break;
                }
                std::string pattern = tokens[1];
                int overallRetries = 0;
                bool success = false;
                while (!success && overallRetries < 2) {
                    if (performListUsers(pattern)) {
                        success = true;
                    } else {
                        overallRetries++;
                        PrintClientDebug("List users failed after 3 attempts. Re-discovering primary and retrying list.");
                        std::vector<std::string> serverList = loadServerAddresses("servers.txt");
                        std::string newPrimary = getPrimaryAddress(serverList);
                        if (!newPrimary.empty())
                            updateStubToPrimary(newPrimary);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                if (!success) {
                    PrintJson("{\"type\":\"error\", \"message\":\"Failed to retrieve user list.\"}");
                }
                break;
            }
            case 'd': { // Delete message
                if (tokens.size() < 3) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid delete message command.\"}");
                    break;
                }
                std::string username = tokens[1];
                std::string message_id = tokens[2];
                int overallRetries = 0;
                bool success = false;
                while (!success && overallRetries < 2) {
                    if (performDeleteMessage(username, message_id)) {
                        success = true;
                    } else {
                        overallRetries++;
                        PrintClientDebug("Delete message failed after 3 attempts. Re-discovering primary and retrying deletion.");
                        std::vector<std::string> serverList = loadServerAddresses("servers.txt");
                        std::string newPrimary = getPrimaryAddress(serverList);
                        if (!newPrimary.empty())
                            updateStubToPrimary(newPrimary);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                if (!success) {
                    PrintJson("{\"type\":\"error\", \"message\":\"Failed to delete message.\"}");
                }
                break;
            }
            case 'q': { // Quit
                PrintClientDebug("Quit command received. Stopping client loop.");
                g_keep_running = false;
                break;
            }
            default:
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Unknown command.\"}");
                break;
        }
    }
    PrintClientDebug("Main input loop ended; STDIN closed or g_keep_running is false.");
}


/*
 * main - Entry point.
 * No server IP/port is provided by the user; the list is read from servers.txt.
 * The client expects one command-line argument: its delivery address (e.g., "192.168.1.100:6000").
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "[CLIENT] Usage: " << argv[0] << " <delivery_address>\n";
        return 1;
    }
    // Set the client's delivery address from the command line.
    g_delivery_address = argv[1];
    
    // Start the DeliveryService in a separate thread.
    std::thread deliveryThread(RunDeliveryService, g_delivery_address);
    deliveryThread.detach();
    
    // Load server addresses from servers.txt.
    std::vector<std::string> serverList = loadServerAddresses("servers.txt");
    if (serverList.empty()) {
        std::cerr << "[CLIENT] No servers found in servers.txt\n";
        return 1;
    }
    // Determine the primary server from the list.
    std::string primaryAddress = "";
    for (const auto &addr : serverList) {
        PrintClientDebug("Attempting connection to server: " + addr);
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        std::unique_ptr<ChatService::Stub> stub = ChatService::NewStub(channel);
        LeaderRequest req;
        req.set_client_id("client_dummy");
        LeaderResponse resp;
        ClientContext context;
        Status status = stub->GetLeaderInfo(&context, req, &resp);
        if (status.ok() && !resp.leader_address().empty()) {
            primaryAddress = resp.leader_address();
            PrintClientDebug("Primary determined as: " + primaryAddress);
            break;
        }
    }
    if (primaryAddress.empty()) {
        std::cerr << "[CLIENT] Failed to determine a primary server.\n";
        return 1;
    }
    
    PrintClientDebug("Starting client with primary address: " + primaryAddress);
    RunClient(primaryAddress);
    return 0;
}
