// test-grpc.cpp
#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"
#include "user_auth/user_auth.h"

// Include your service implementation.
// Ensure that ChatServiceImpl and global variables are accessible.
// (Adjust the include as needed for your project organization.)
#include "server-grpc.cpp"  

#include <thread>
#include <chrono>
#include <mutex>
#include <sstream>
#include <vector>
#include <unordered_map>

// Using declarations.
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ClientContext;
using grpc::Channel;
using grpc::InsecureServerCredentials;
using grpc::InsecureChannelCredentials;

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

// Extern declarations for global variables from your server code.
// (These must be declared in a header or removed if your service encapsulates them.)
extern std::mutex user_mutex;
extern std::unordered_map<std::string, UserInfo> user_map;
extern std::mutex messages_mutex;
extern std::unordered_map<int, Message> messages;
extern int messageCounter;
extern std::mutex stream_mutex;
extern std::unordered_map<std::string, ServerReaderWriter<ChatMessage, ChatMessage>*> activeStreams;

// Global pointers for the in‑process server.
std::unique_ptr<Server> g_test_server;
std::unique_ptr<ChatService::Service> g_service;

// Helper function: Create an in‑process channel.
std::shared_ptr<Channel> CreateInProcessChannel(const std::string &server_address) {
    ServerBuilder builder;
    builder.AddListeningPort(server_address, InsecureServerCredentials());
    g_service.reset(new ChatServiceImpl());
    builder.RegisterService(g_service.get());
    g_test_server = builder.BuildAndStart();
    return g_test_server->InProcessChannel(grpc::ChannelArguments());
}

// Test Registration and Duplicate Registration.
TEST(ChatServiceTest, RegistrationTest) {
    std::string server_address = "inproc://chat_test";
    auto channel = CreateInProcessChannel(server_address);
    auto stub = ChatService::NewStub(channel);

    RegisterRequest req;
    RegisterResponse resp;
    ClientContext context;
    req.set_username("testuser");
    req.set_password("testpass");
    Status status = stub->Register(&context, req, &resp);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(resp.success());
    EXPECT_EQ(resp.message(), "User registered successfully.");

    // Duplicate registration should fail.
    ClientContext context2;
    RegisterRequest req2;
    RegisterResponse resp2;
    req2.set_username("testuser");
    req2.set_password("newpass");
    status = stub->Register(&context2, req2, &resp2);
    EXPECT_TRUE(status.ok());
    EXPECT_FALSE(resp2.success());
    EXPECT_EQ(resp2.message(), "Username already exists.");
}

// Test Login: both successful and failed.
TEST(ChatServiceTest, LoginTest) {
    std::string server_address = "inproc://chat_test";
    auto channel = CreateInProcessChannel(server_address);
    auto stub = ChatService::NewStub(channel);

    // Register a user for login.
    {
        RegisterRequest req;
        RegisterResponse resp;
        ClientContext context;
        req.set_username("loginuser");
        req.set_password("mypassword");
        Status status = stub->Register(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        EXPECT_TRUE(resp.success());
    }

    // Successful login.
    {
        LoginRequest req;
        LoginResponse resp;
        ClientContext context;
        req.set_username("loginuser");
        req.set_password("mypassword");
        Status status = stub->Login(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        EXPECT_TRUE(resp.success());
        EXPECT_EQ(resp.message(), "Login successful.");
    }

    // Failed login: wrong password.
    {
        LoginRequest req;
        LoginResponse resp;
        ClientContext context;
        req.set_username("loginuser");
        req.set_password("wrongpass");
        Status status = stub->Login(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        EXPECT_FALSE(resp.success());
        EXPECT_EQ(resp.message(), "Invalid username or password.");
    }
}

// Test ChatStream functionality: sending a message and receiving an ACK.
TEST(ChatServiceTest, ChatStreamTest) {
    std::string server_address = "inproc://chat_test";
    auto channel = CreateInProcessChannel(server_address);
    auto stub = ChatService::NewStub(channel);

    // Open a ChatStream for "alice".
    ClientContext context_alice;
    auto alice_stream = stub->ChatStream(&context_alice);
    ChatMessage init_msg;
    init_msg.set_sender("alice");
    EXPECT_TRUE(alice_stream->Write(init_msg));

    // Ensure recipient "bob" exists and is online.
    {
        std::lock_guard<std::mutex> lock(user_mutex);
        if (user_map.find("bob") == user_map.end()) {
            UserInfo bob;
            bob.password = "dummy";
            bob.isOnline = true;
            user_map["bob"] = bob;
        } else {
            user_map["bob"].isOnline = true;
        }
    }

    // Simulate "alice" sending a message to "bob".
    ChatMessage chat_msg;
    chat_msg.set_sender("alice");
    chat_msg.set_recipient("bob");
    chat_msg.set_message("Hello, Bob!");
    EXPECT_TRUE(alice_stream->Write(chat_msg));

    // Read the ACK from the server.
    ChatMessage ack_msg;
    EXPECT_TRUE(alice_stream->Read(&ack_msg));
    EXPECT_EQ(ack_msg.sender(), "server");
    EXPECT_EQ(ack_msg.message(), "ACK");
    EXPECT_NE(ack_msg.message_id(), "-1");

    alice_stream->WritesDone();
    Status status = alice_stream->Finish();
    EXPECT_TRUE(status.ok());
}

// Test offline message retrieval.
TEST(ChatServiceTest, RetrieveOfflineMessagesTest) {
    std::string server_address = "inproc://chat_test";
    auto channel = CreateInProcessChannel(server_address);
    auto stub = ChatService::NewStub(channel);

    // Register user "eve".
    {
        RegisterRequest req;
        RegisterResponse resp;
        ClientContext context;
        req.set_username("eve");
        req.set_password("secret");
        Status status = stub->Register(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        EXPECT_TRUE(resp.success());
    }
    // Manually add 3 offline messages for "eve".
    {
        std::lock_guard<std::mutex> lock(user_mutex);
        auto it = user_map.find("eve");
        ASSERT_NE(it, user_map.end());
        for (int i = 0; i < 3; i++) {
            Message msg;
            msg.id = std::to_string(300 + i);
            msg.sender = "frank";
            msg.recipient = "eve";
            msg.content = "Offline message " + std::to_string(i + 1);
            msg.delivered = false;
            it->second.offlineMessages.push_back(msg);
        }
    }
    // Retrieve with max_messages = 2.
    {
        UndeliveredMessagesRequest req;
        req.set_username("eve");
        req.set_max_messages(2);
        UndeliveredMessagesResponse resp;
        ClientContext context;
        Status status = stub->RetrieveUndeliveredMessages(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(resp.messages_size(), 2);
    }
    // Retrieve again; expect 1 remaining.
    {
        UndeliveredMessagesRequest req;
        req.set_username("eve");
        req.set_max_messages(5);
        UndeliveredMessagesResponse resp;
        ClientContext context;
        Status status = stub->RetrieveUndeliveredMessages(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(resp.messages_size(), 1);
    }
}

// Test DeleteMessage RPC.
TEST(ChatServiceTest, DeleteMessageTest) {
    std::string server_address = "inproc://chat_test";
    auto channel = CreateInProcessChannel(server_address);
    auto stub = ChatService::NewStub(channel);

    // Register user "gina".
    {
        RegisterRequest req;
        RegisterResponse resp;
        ClientContext context;
        req.set_username("gina");
        req.set_password("123");
        Status status = stub->Register(&context, req, &resp);
        EXPECT_TRUE(status.ok());
    }
    // Simulate adding a message.
    int localMsgId;
    {
        std::lock_guard<std::mutex> lock(messages_mutex);
        messageCounter++;
        localMsgId = messageCounter;
        Message msg;
        msg.id = std::to_string(localMsgId);
        msg.content = "Message to delete";
        msg.sender = "frank";
        msg.recipient = "gina";
        msg.delivered = true;
        messages[localMsgId] = msg;
    }
    // Delete the message.
    DeleteMessageRequest dreq;
    DeleteMessageResponse dresp;
    ClientContext dcontext;
    dreq.set_message_id(std::to_string(localMsgId));
    dreq.set_requesting_user("gina");
    Status status = stub->DeleteMessage(&dcontext, dreq, &dresp);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(dresp.success());
}

// Test SearchUsers RPC.
TEST(ChatServiceTest, SearchUsersTest) {
    std::string server_address = "inproc://chat_test";
    auto channel = CreateInProcessChannel(server_address);
    auto stub = ChatService::NewStub(channel);

    // Register several users.
    {
        RegisterRequest req;
        RegisterResponse resp;
        ClientContext context;
        req.set_username("alice_test");
        req.set_password("pass");
        Status status = stub->Register(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        req.set_username("bob_test");
        status = stub->Register(&context, req, &resp);
        EXPECT_TRUE(status.ok());
        req.set_username("charlie_test");
        status = stub->Register(&context, req, &resp);
        EXPECT_TRUE(status.ok());
    }
    // Search for users containing "test".
    SearchUsersRequest sreq;
    sreq.set_wildcard("test");
    SearchUsersResponse sresp;
    ClientContext context;
    Status status = stub->SearchUsers(&context, sreq, &sresp);
    EXPECT_TRUE(status.ok());
    EXPECT_GE(sresp.usernames_size(), 3);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    if (g_test_server) {
        g_test_server->Shutdown();
    }
    return result;
}
