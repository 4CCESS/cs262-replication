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

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ClientReaderWriter;

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

// Global variables for the ChatStream.
std::unique_ptr<ChatService::Stub> g_stub;
std::shared_ptr<ClientReaderWriter<ChatMessage, ChatMessage>> g_chat_stream;
std::mutex g_stream_write_mutex;
std::atomic<bool> g_streaming_active(false);
std::atomic<bool> g_keep_running(true);
std::string g_current_user;
std::mutex g_cout_mutex;

// Helper: Thread-safe JSON output (for responses intended for the GUI).
void PrintJson(const std::string &json_str) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << json_str << std::endl;
}

// Helper: Debug logging that prefixes messages with "[CLIENT] "
void PrintClientDebug(const std::string &msg) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << "[CLIENT] " << msg << std::endl;
}

// Helper: Build a JSON string from provided fields.
std::string JsonString(const std::string &type, const std::string &sender,
                       const std::string &message_id, const std::string &recipient,
                       const std::string &content) {
    std::ostringstream oss;
    oss << "{\"type\":\"" << type << "\","
        << "\"sender\":\"" << sender << "\","
        << "\"message_id\":\"" << message_id << "\","
        << "\"recipient\":\"" << recipient << "\","
        << "\"content\":\"" << content << "\"}";
    return oss.str();
}

// ChatStream thread: opens the bidirectional stream, sends the init message,
// and continuously reads incoming messages.
void ChatStreamThread() {
    PrintClientDebug("Starting ChatStreamThread...");
    ClientContext context;
    g_chat_stream = g_stub->ChatStream(&context);
    
    // Send init message automatically (content is irrelevant; we use sender to identify the user).
    ChatMessage init_msg;
    init_msg.set_sender(g_current_user);
    PrintClientDebug("Sending init message for user: " + g_current_user);
    if (!g_chat_stream->Write(init_msg)) {
        PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Failed to send init message.\"}");
        return;
    }
    g_streaming_active = true;
    
    ChatMessage incoming;
    while (g_keep_running.load() && g_chat_stream->Read(&incoming)) {
        // Debug: log raw incoming message details.
        PrintClientDebug("Received message from: " + incoming.sender() +
                         " | to: " + incoming.recipient() +
                         " | content: " + incoming.message());
        if (incoming.sender() == "server" && incoming.message() == "ACK") {
            std::string ack_json = JsonString("confirmation", incoming.sender(),
                                              incoming.message_id(), incoming.recipient(), incoming.message());
            PrintJson(ack_json);
        } else {
            std::string chat_json = JsonString("chat", incoming.sender(),
                                               incoming.message_id(), incoming.recipient(), incoming.message());
            PrintJson(chat_json);
        }
    }
    g_streaming_active = false;
    PrintJson("{\"type\":\"system\", \"message\":\"[CLIENT] Chat stream closed.\"}");
}

// Main client loop: reads commands from stdin and issues appropriate RPC calls.
void RunClient(const std::string &server_address) {
    PrintClientDebug("Creating channel to " + server_address);
    auto channel = grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials());
    g_stub = ChatService::NewStub(channel);

    // DELIMITER (ASCII Unit Separator) used to split commands.
    const char DELIMITER = '\x1F';
    std::string line;
    PrintClientDebug("Starting main input loop...");
    while (g_keep_running.load() && std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        PrintClientDebug("Raw input: " + line);
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (std::getline(iss, token, DELIMITER)) {
            tokens.push_back(token);
        }
        if (tokens.empty())
            continue;
        char op = tokens[0][0];
        PrintClientDebug("Parsed operation: " + std::string(1, op));
        
        if (op == 'L' || op == 'R') {
            if (tokens.size() < 3) {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid login/register command.\"}");
                continue;
            }
            std::string username = tokens[1];
            std::string password = tokens[2];
            if (op == 'L') {
                PrintClientDebug("Sending Login RPC for user: " + username);
                LoginRequest req;
                req.set_username(username);
                req.set_password(password);
                std::cout << "[MSGSIZE] Login: " << req.ByteSizeLong() 
                    << " bytes" << std::endl;
                LoginResponse resp;
                ClientContext context;
                Status status = g_stub->Login(&context, req, &resp);
                if (status.ok() && resp.success()) {
                    g_current_user = username;
                    PrintJson("{\"type\":\"validation\", \"message\":\"authentication success\"}");
                    // Automatically open the ChatStream.
                    std::thread stream_thread(ChatStreamThread);
                    stream_thread.detach();
                } else {
                    PrintJson("{\"type\":\"validation\", \"message\":\"authentication fail\"}");
                }
            } else {  // Registration.
                PrintClientDebug("Sending Register RPC for user: " + username);
                RegisterRequest req;
                req.set_username(username);
                req.set_password(password);
                std::cout << "[MSGSIZE] Register: " << req.ByteSizeLong() 
                    << " bytes" << std::endl;
                RegisterResponse resp;
                ClientContext context;
                Status status = g_stub->Register(&context, req, &resp);
                if (status.ok() && resp.success()) {
                    PrintJson("{\"type\":\"validation\", \"message\":\"[CLIENT] Registration successful.\"}");
                } else {
                    PrintJson("{\"type\":\"validation\", \"message\":\"[CLIENT] Registration failed.\"}");
                }
            }
        }
        else if (op == 's') {
            if (tokens.size() < 4) {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid send message command.\"}");
                continue;
            }
            if (!g_streaming_active.load()) {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Chat stream is not active.\"}");
                continue;
            }
            std::string sender = tokens[1];
            std::string recipient = tokens[2];
            std::string message = tokens[3];
            PrintClientDebug("Sending message from " + sender + " to " + recipient + ": " + message);
            // Queue the outgoing message so it can later be paired with the ACK.
            // (The GUI will handle pairing via pending_messages; the client simply sends.)
            ChatMessage msg;
            msg.set_sender(sender);
            msg.set_recipient(recipient);
            msg.set_message(message);
            {
                std::lock_guard<std::mutex> lock(g_stream_write_mutex);
                if (!g_chat_stream->Write(msg)) {
                    PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Failed to send message via stream.\"}");
                }
            }
            std::cout << "[MSGSIZE] Sent Message: " << msg.ByteSizeLong() 
                << " bytes" << std::endl;
        }
        else if (op == 'r') {
            // Offline message retrieval.
            if (tokens.size() < 3) {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid retrieval command.\"}");
                continue;
            }
            std::string username = tokens[1];
            int max_messages = std::stoi(tokens[2]);
            PrintClientDebug("Sending RetrieveUndeliveredMessages RPC for user: " + username +
                             " with max_messages: " + std::to_string(max_messages));
            UndeliveredMessagesRequest req;
            req.set_username(username);
            req.set_max_messages(max_messages);
            UndeliveredMessagesResponse resp;
            ClientContext context;
            Status status = g_stub->RetrieveUndeliveredMessages(&context, req, &resp);
            if (status.ok()) {
                for (int i = 0; i < resp.messages_size(); i++) {
                    const ChatMessage &msg = resp.messages(i);
                    std::string json = JsonString("chat", msg.sender(), msg.message_id(), msg.recipient(), msg.message());
                    PrintJson(json);
                }
                PrintClientDebug("Retrieved " + std::to_string(resp.messages_size()) + " offline messages.");
            } else {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Failed to retrieve offline messages.\"}");
            }
        }
        else if (op == 'l') {
            if (tokens.size() < 2) {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid list users command.\"}");
                continue;
            }
            std::string pattern = tokens[1];
            PrintClientDebug("Sending SearchUsers RPC for pattern: " + pattern);
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
            } else {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Failed to retrieve user list.\"}");
            }
        }
        else if (op == 'd') {
            if (tokens.size() < 3) {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Invalid delete message command.\"}");
                continue;
            }
            std::string username = tokens[1];
            std::string message_id = tokens[2];
            PrintClientDebug("Sending DeleteMessage RPC for message id: " + message_id);
            DeleteMessageRequest req;
            req.set_message_id(message_id);
            req.set_requesting_user(username);
            DeleteMessageResponse resp;
            ClientContext context;
            Status status = g_stub->DeleteMessage(&context, req, &resp);
            if (status.ok() && resp.success()) {
                PrintJson("{\"type\":\"delete\", \"message_id\":\"[CLIENT] " + message_id + "\"}");
            } else {
                PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Failed to delete message.\"}");
            }
        }
        else if (op == 'q') {
            PrintClientDebug("Quit command received. Stopping client loop.");
            g_keep_running = false;
            break;
        }
        else {
            PrintJson("{\"type\":\"error\", \"message\":\"[CLIENT] Unknown command.\"}");
        }
    }
    PrintClientDebug("Main input loop ended; STDIN closed or g_keep_running is false.");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {  // Now expecting 2 arguments: server_ip and port
        std::cerr << "[CLIENT] Usage: " << argv[0] << " <server_ip> <port>\n";
        return 15;
    }
    // Combine the two arguments into one string (server_address:port)
    std::string server_address = std::string(argv[1]) + ":" + std::string(argv[2]);
    PrintClientDebug("Starting client with server_address: " + server_address);
    RunClient(server_address);
    return 0;
}
