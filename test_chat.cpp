// test_chat.cpp
#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"
#include <google/protobuf/empty.pb.h>

#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <sstream>

// Secure authentication suite (for password hashing, etc.)
#include "user_auth/user_auth.h"

// ----------------------------------------------------------------------
// Global in-memory data structures to simulate persistent state.
// In a real system these would be in the server implementation (with SQLite persistence).
// ----------------------------------------------------------------------

// User and Message structures used by the server.
struct UserInfoTest {
  std::string password;
  bool isOnline = false;
  std::string delivery_address;
  // For testing, offline messages are stored as ChatMessage objects.
  std::vector<chat::ChatMessage> offlineMessages;
};

struct MessageTest {
  std::string id;
  std::string content;
  std::string sender;
  std::string recipient;
  bool delivered = false;
};

// Global in-memory maps and counters.
static std::mutex g_user_mutex;
static std::unordered_map<std::string, UserInfoTest> g_user_map;

static std::mutex g_messages_mutex;
static std::unordered_map<int, MessageTest> g_messages;
static int g_messageCounter = 0;

// Dummy persistence functions (always succeed in tests).
bool persistUser(const std::string& username, const UserInfoTest& user) { return true; }
bool persistMessage(int id, const MessageTest& msg) { return true; }
bool removeMessage(int id) { return true; }

// ----------------------------------------------------------------------
// Dummy implementations of service classes for testing.
// These classes mimic the real ChatServiceImpl and ElectionServiceImpl.
// ----------------------------------------------------------------------

using grpc::ServerContext;
using grpc::Status;

// ChatService implementation for unit testing.
class ChatServiceImplTest : public chat::ChatService::Service {
public:
  // In-memory user map and message map are simulated via the globals above.
  
  // Register a new user.
  Status Register(ServerContext* context, const chat::RegisterRequest* request,
                  chat::RegisterResponse* response) override {
    std::lock_guard<std::mutex> lock(g_user_mutex);
    std::string username = request->username();
    if (g_user_map.find(username) != g_user_map.end()) {
      response->set_success(true);
      response->set_message("Username already exists.");
      return Status::OK;
    }
    UserInfoTest newUser;
    newUser.password = request->password();
    newUser.isOnline = false;
    newUser.delivery_address = "";
    g_user_map[username] = newUser;
    persistUser(username, newUser);
    response->set_success(true);
    response->set_message("Registration successful.");
    return Status::OK;
  }

  // Login: verifies credentials and marks user online.
  // Also returns a history of messages where the user was sender or (delivered) recipient.
  Status Login(ServerContext* context, const chat::LoginRequest* request,
               chat::LoginResponse* response) override {
    std::lock_guard<std::mutex> lock(g_user_mutex);
    std::string username = request->username();
    auto it = g_user_map.find(username);
    if (it == g_user_map.end() || it->second.password != request->password()) {
      response->set_success(false);
      response->set_message("Invalid username or password.");
      return Status::OK;
    }
    it->second.isOnline = true;
    it->second.delivery_address = request->delivery_address();
    persistUser(username, it->second);
    response->set_success(true);
    response->set_session_token("dummy_token");
    response->set_message("Login successful.");
    
    // Add history messages: include messages where the user is sender OR
    // recipient and the message is marked as delivered.
    std::lock_guard<std::mutex> lockMsg(g_messages_mutex);
    for (const auto &pair : g_messages) {
      const MessageTest &msg = pair.second;
      if (msg.sender == username || (msg.recipient == username && msg.delivered)) {
        chat::ChatMessage* chatMsg = response->add_messages();
        chatMsg->set_sender(msg.sender);
        chatMsg->set_recipient(msg.recipient);
        chatMsg->set_message(msg.content);
        chatMsg->set_timestamp(""); // Timestamp omitted for simplicity.
        chatMsg->set_message_id(msg.id);
      }
    }
    return Status::OK;
  }

  // SendMessage: Generates a unique message, persists it, and attempts delivery.
  Status SendMessage(ServerContext* context, const chat::ChatMessage* request,
                     chat::MessageResponse* response) override {
    std::string sender = request->sender();
    std::string recipient = request->recipient();
    std::string msg_text = request->message();
    std::string timestamp = request->timestamp();
    
    int id;
    std::string msg_id;
    {
      std::lock_guard<std::mutex> lock(g_messages_mutex);
      g_messageCounter++;
      id = g_messageCounter;
      msg_id = std::to_string(id);
    }
    
    MessageTest msg;
    msg.id = msg_id;
    msg.content = msg_text;
    msg.sender = sender;
    msg.recipient = recipient;
    // For this test, we assume delivery always succeeds.
    msg.delivered = true;
    {
      std::lock_guard<std::mutex> lock(g_messages_mutex);
      g_messages[id] = msg;
    }
    persistMessage(id, msg);
    
    // Build replication update (we assume replication always succeeds in tests)
    // In a full test, you might call a function replicateUpdateToBackups(update).
    
    // Return send acknowledgment.
    response->set_success(true);
    response->set_message_id(msg_id);
    response->set_message("Message accepted and delivered.");
    return Status::OK;
  }

  // RetrieveUndeliveredMessages: Returns offline messages and marks them as delivered.
  Status RetrieveUndeliveredMessages(ServerContext* context, const chat::UndeliveredMessagesRequest* request,
                                     chat::UndeliveredMessagesResponse* response) override {
    std::lock_guard<std::mutex> lock(g_user_mutex);
    std::string username = request->username();
    auto it = g_user_map.find(username);
    if (it == g_user_map.end()) {
      return Status(grpc::NOT_FOUND, "User not found.");
    }
    int max_messages = request->max_messages();
    int count = 0;
    std::vector<chat::ChatMessage> toDeliver;
    // In this test version, assume that offline messages are stored in the user's offlineMessages.
    for (const auto &msg : it->second.offlineMessages) {
      if (count >= max_messages) break;
      chat::ChatMessage* chatMsg = response->add_messages();
      chatMsg->set_sender(msg.sender());
      chatMsg->set_recipient(msg.recipient());
      chatMsg->set_message(msg.message());
      chatMsg->set_timestamp(msg.timestamp());
      chatMsg->set_message_id(msg.message_id());
      toDeliver.push_back(msg);
      count++;
    }
    // Mark them as delivered (update persistent storage simulation).
    it->second.offlineMessages.erase(it->second.offlineMessages.begin(),
                                     it->second.offlineMessages.begin() + count);
    persistUser(username, it->second);
    return Status::OK;
  }

  // DeleteMessage: Deletes a message if the requesting user is the recipient.
  Status DeleteMessage(ServerContext* context, const chat::DeleteMessageRequest* request,
                       chat::DeleteMessageResponse* response) override {
    int msg_id = std::stoi(request->message_id());
    std::lock_guard<std::mutex> lock(g_messages_mutex);
    auto it = g_messages.find(msg_id);
    if (it == g_messages.end() || it->second.recipient != request->requesting_user()) {
      response->set_success(false);
      return Status::OK;
    }
    g_messages.erase(it);
    removeMessage(msg_id);
    response->set_success(true);
    return Status::OK;
  }

  // SearchUsers: Returns usernames matching a wildcard.
  Status SearchUsers(ServerContext* context, const chat::SearchUsersRequest* request,
                     chat::SearchUsersResponse* response) override {
    std::lock_guard<std::mutex> lock(g_user_mutex);
    for (const auto &kv : g_user_map) {
      if (kv.first.find(request->wildcard()) != std::string::npos) {
        response->add_usernames(kv.first);
      }
    }
    return Status::OK;
  }
};

// Minimal ElectionService implementation for tests.
class ElectionServiceImplTest : public chat::ElectionService::Service {
public:
  Status SendScore(ServerContext* context, const chat::ScoreRequest* request,
                   chat::ScoreResponse* response) override {
    response->set_received(true);
    return Status::OK;
  }
  Status AnnounceLeader(ServerContext* context, const chat::LeaderAnnouncement* request,
                        chat::Ack* response) override {
    response->set_success(true);
    return Status::OK;
  }
};

// ----------------------------------------------------------------------
// Dummy ServerContext for testing purposes.
class DummyServerContext : public ServerContext {
public:
  DummyServerContext() {}
};

// ----------------------------------------------------------------------
// Unit Tests
// ----------------------------------------------------------------------

TEST(ChatServiceTest, RegisterAndLogin) {
  ChatServiceImplTest service;
  DummyServerContext context;
  
  // Test registration.
  chat::RegisterRequest regReq;
  regReq.set_username("testuser");
  regReq.set_password("testpass");
  chat::RegisterResponse regResp;
  Status regStatus = service.Register(&context, &regReq, &regResp);
  EXPECT_TRUE(regStatus.ok());
  EXPECT_TRUE(regResp.success());
  EXPECT_EQ(regResp.message(), "Registration successful.");
  
  // Test login with correct credentials.
  chat::LoginRequest loginReq;
  loginReq.set_username("testuser");
  loginReq.set_password("testpass");
  loginReq.set_delivery_address("127.0.0.1:6000");
  chat::LoginResponse loginResp;
  Status loginStatus = service.Login(&context, &loginReq, &loginResp);
  EXPECT_TRUE(loginStatus.ok());
  EXPECT_TRUE(loginResp.success());
  EXPECT_EQ(loginResp.message(), "Login successful.");
  // Since no messages have been sent yet, history should be empty.
  EXPECT_EQ(loginResp.messages_size(), 0);
}

TEST(ChatServiceTest, SendMessageAndRetrieve) {
  ChatServiceImplTest service;
  DummyServerContext context;
  
  // Register two users: sender and recipient.
  {
    chat::RegisterRequest regReq;
    regReq.set_username("alice");
    regReq.set_password("alicepass");
    chat::RegisterResponse regResp;
    service.Register(&context, &regReq, &regResp);
    
    regReq.set_username("bob");
    regReq.set_password("bobpass");
    service.Register(&context, &regReq, &regResp);
    
    // Login bob (simulate online)
    chat::LoginRequest loginReq;
    loginReq.set_username("bob");
    loginReq.set_password("bobpass");
    loginReq.set_delivery_address("127.0.0.1:6001");
    chat::LoginResponse loginResp;
    service.Login(&context, &loginReq, &loginResp);
  }
  
  // Alice sends a message to Bob.
  chat::ChatMessage sendReq;
  sendReq.set_sender("alice");
  sendReq.set_recipient("bob");
  sendReq.set_message("Hello, Bob!");
  sendReq.set_timestamp("2022-01-01T12:00:00Z");
  chat::MessageResponse msgResp;
  Status sendStatus = service.SendMessage(&context, &sendReq, &msgResp);
  EXPECT_TRUE(sendStatus.ok());
  EXPECT_TRUE(msgResp.success());
  std::string msgId = msgResp.message_id();
  EXPECT_FALSE(msgId.empty());
  
  // Now simulate retrieval of undelivered messages.
  // For testing, we first add a message to Bob's offline messages.
  {
    std::lock_guard<std::mutex> lock(g_user_mutex);
    chat::ChatMessage offlineMsg;
    offlineMsg.set_sender("alice");
    offlineMsg.set_recipient("bob");
    offlineMsg.set_message("Offline hello from Alice.");
    offlineMsg.set_timestamp("2022-01-01T12:05:00Z");
    offlineMsg.set_message_id("offline1");
    g_user_map["bob"].offlineMessages.push_back(offlineMsg);
  }
  
  chat::UndeliveredMessagesRequest retReq;
  retReq.set_username("bob");
  retReq.set_max_messages(10);
  chat::UndeliveredMessagesResponse retResp;
  Status retStatus = service.RetrieveUndeliveredMessages(&context, &retReq, &retResp);
  EXPECT_TRUE(retStatus.ok());
  // We expect at least one offline message.
  EXPECT_GT(retResp.messages_size(), 0);
  
  // Check that retrieved messages are marked as delivered in persistent storage.
  {
    std::lock_guard<std::mutex> lock(g_messages_mutex);
    // (For testing, our dummy persistence functions don't change state,
    // but we could check that our in-memory offlineMessages vector was cleared.)
    auto it = g_user_map.find("bob");
    EXPECT_TRUE(it != g_user_map.end());
    EXPECT_EQ(it->second.offlineMessages.size(), 0);
  }
}

TEST(ChatServiceTest, DeleteMessage) {
  ChatServiceImplTest service;
  DummyServerContext context;
  
  // Register and login a user.
  {
    chat::RegisterRequest regReq;
    regReq.set_username("charlie");
    regReq.set_password("charliepass");
    chat::RegisterResponse regResp;
    service.Register(&context, &regReq, &regResp);
    
    chat::LoginRequest loginReq;
    loginReq.set_username("charlie");
    loginReq.set_password("charliepass");
    loginReq.set_delivery_address("127.0.0.1:6002");
    chat::LoginResponse loginResp;
    service.Login(&context, &loginReq, &loginResp);
  }
  
  // Charlie sends a message to himself.
  chat::ChatMessage msgReq;
  msgReq.set_sender("charlie");
  msgReq.set_recipient("charlie");
  msgReq.set_message("This message will be deleted.");
  msgReq.set_timestamp("2022-01-01T13:00:00Z");
  chat::MessageResponse msgResp;
  Status sendStatus = service.SendMessage(&context, &msgReq, &msgResp);
  EXPECT_TRUE(sendStatus.ok());
  EXPECT_TRUE(msgResp.success());
  std::string msgId = msgResp.message_id();
  EXPECT_FALSE(msgId.empty());
  
  // Delete the message.
  chat::DeleteMessageRequest delReq;
  delReq.set_message_id(msgId);
  delReq.set_requesting_user("charlie");
  chat::DeleteMessageResponse delResp;
  Status delStatus = service.DeleteMessage(&context, &delReq, &delResp);
  EXPECT_TRUE(delStatus.ok());
  EXPECT_TRUE(delResp.success());
}

TEST(ChatServiceTest, SearchUsers) {
  ChatServiceImplTest service;
  DummyServerContext context;
  
  // Register two users.
  {
    chat::RegisterRequest regReq;
    regReq.set_username("david");
    regReq.set_password("pass1");
    chat::RegisterResponse regResp;
    service.Register(&context, &regReq, &regResp);
    
    regReq.set_username("daniel");
    regReq.set_password("pass2");
    service.Register(&context, &regReq, &regResp);
  }
  
  // Search for "da"
  chat::SearchUsersRequest searchReq;
  searchReq.set_wildcard("da");
  chat::SearchUsersResponse searchResp;
  Status searchStatus = service.SearchUsers(&context, &searchReq, &searchResp);
  EXPECT_TRUE(searchStatus.ok());
  EXPECT_GT(searchResp.usernames_size(), 0);
  bool foundDavid = false;
  for (int i = 0; i < searchResp.usernames_size(); i++) {
    if (searchResp.usernames(i) == "david")
      foundDavid = true;
  }
  EXPECT_TRUE(foundDavid);
}

// Main function for running all tests.
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
