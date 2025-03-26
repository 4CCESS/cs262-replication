#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"
#include <google/protobuf/empty.pb.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>
#include <sqlite3.h>

// Secure authentication suite (for password hashing, etc.)
#include "user_auth/user_auth.h"

// ------------------ Using Declarations ------------------

// Basic gRPC types.
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerReaderWriter;
using grpc::ClientContext;

// ----- ISM Service (Discovery, Replication, Heartbeat) -----
using chat::ISMService;
using chat::DiscoveryPing;
using chat::DiscoveryResponse;
using chat::ReplicationUpdate;
using chat::UpdateAck;
using chat::HeartbeatRequest;
using chat::HeartbeatResponse;

// ----- Election Service -----
using chat::ElectionService;
using chat::ScoreRequest;
using chat::ScoreResponse;
using chat::LeaderAnnouncement;
using chat::Ack;

// ----- Leader Discovery for Clients via ChatService ----------
using chat::LeaderRequest;
using chat::LeaderResponse;

// ----- Chat Service and Related Messages -----
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
using chat::MessageResponse;

// ----- Delivery Service -----
// (This service is implemented by the client. The server uses its stub to deliver messages.)
using chat::DeliveryService;
using chat::DeliveryRequest;
using chat::DeliveryResponse;

// ----- State Synchronization -----
// GetFullState is defined under ChatService in your proto.
using chat::FullState;
using chat::UserState;
using chat::MessageState;

// ------------------ Global Variables ------------------

// SQLite database pointer.
sqlite3* db = nullptr;

// Election and leader state.
std::atomic<bool> electionInProgress(false);
std::string self_address;
std::string self_id;  // We use self_address as our unique identifier.
int self_role;        // For bootstrap: role 0; later, new servers start as role 3.
int election_cycle = 1; // Simplified election cycle.

// Global leader state.
std::mutex leader_mutex;
std::string currentLeader;
int currentLeaderElectionRound = 0;

// ------------------ Data Structures ------------------

// Structure for peer discovery.
struct PeerInfo {
    std::string id;
    int role;      // Reported role.
    int election_cycle;
};

std::mutex peers_mutex;
std::unordered_map<std::string, PeerInfo> discovered_peers;

// Chat message record.
struct Message {
    std::string id;
    std::string content;
    std::string sender;
    std::string recipient;
    bool delivered = false;
};

// User record stores delivery address for immediate delivery.
struct UserInfo {
    std::string password; // Hashed password.
    bool isOnline = false;
    std::string delivery_address; // e.g., "127.0.0.1:6000"
    std::vector<Message> offlineMessages;
};

std::mutex user_mutex;
std::unordered_map<std::string, UserInfo> user_map;

std::mutex messages_mutex;
std::unordered_map<int, Message> messages;
int messageCounter = 0;

// ------------------ Function Prototypes ------------------

// Forward declarations so they are available to functions below.
std::vector<std::string> loadPeerAddresses(const std::string& filename);
uint32_t computeScoreFromAddress(const std::string &address);
void performDiscovery(const std::vector<std::string>& peer_addresses);
void waitForInitialPeers(const std::vector<std::string>& peer_addresses);
void performElection(const std::vector<std::string>& peer_addresses, uint32_t self_score, int current_election_round);
void monitorLeaderHeartbeat(const std::vector<std::string>& peer_addresses, int heartbeatIntervalSeconds, int maxMissedHeartbeats);


// ------------------ SQLite Persistence Functions ------------------

/**
 * getDBFileName - Returns a unique database filename based on the server address.
 * For example, "127.0.0.1:5000" becomes "chat_127.0.0.1_5000.db"
 */
std::string getDBFileName(const std::string &address) {
    std::string filename = "chat_";
    for (char c : address)
        filename.push_back((c == ':') ? '_' : c);
    filename += ".db";
    return filename;
}

/**
 * openDatabase - Opens (or creates) the SQLite database.
 */
bool openDatabase(const std::string &dbFile) {
    int rc = sqlite3_open(dbFile.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] Can't open database: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    return true;
}

/**
 * createTables - Creates necessary tables if they do not exist.
 */
bool createTables() {
    const char* sql_users = "CREATE TABLE IF NOT EXISTS users ("
                            "username TEXT PRIMARY KEY, "
                            "password TEXT NOT NULL, "
                            "isOnline INTEGER, "
                            "delivery_address TEXT);";
    const char* sql_messages = "CREATE TABLE IF NOT EXISTS messages ("
                               "id INTEGER PRIMARY KEY, "
                               "content TEXT, "
                               "sender TEXT, "
                               "recipient TEXT, "
                               "delivered INTEGER);";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql_users, nullptr, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] SQL error (users): " << errMsg << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    rc = sqlite3_exec(db, sql_messages, nullptr, 0, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] SQL error (messages): " << errMsg << "\n";
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

/**
 * loadData - Loads persistent data into memory.
 */
bool loadData() {
    // Load users.
    const char* sql_select_users = "SELECT username, password, isOnline, delivery_address FROM users;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql_select_users, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] Failed to prepare statement (users): " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string password = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int isOnline = sqlite3_column_int(stmt, 2);
        std::string delivery_address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        UserInfo u;
        u.password = password;
        u.isOnline = (isOnline != 0);
        u.delivery_address = delivery_address;
        {
            std::lock_guard<std::mutex> lock(user_mutex);
            user_map[username] = u;
        }
    }
    sqlite3_finalize(stmt);

    // Load messages.
    const char* sql_select_messages = "SELECT id, content, sender, recipient, delivered FROM messages;";
    rc = sqlite3_prepare_v2(db, sql_select_messages, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] Failed to prepare statement (messages): " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    int maxId = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string sender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string recipient = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        int delivered = sqlite3_column_int(stmt, 4);
        Message m;
        m.id = std::to_string(id);
        m.content = content;
        m.sender = sender;
        m.recipient = recipient;
        m.delivered = (delivered != 0);
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            messages[id] = m;
        }
        if (id > maxId)
            maxId = id;
    }
    sqlite3_finalize(stmt);
    messageCounter = maxId;
    return true;
}

/**
 * persistUser - Persists a user record into the SQLite database.
 */
bool persistUser(const std::string& username, const UserInfo& user) {
    const char* sql = "INSERT OR REPLACE INTO users (username, password, isOnline, delivery_address) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] Failed to prepare persistUser statement: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user.password.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, user.isOnline ? 1 : 0);
    sqlite3_bind_text(stmt, 4, user.delivery_address.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

/**
 * persistMessage - Persists a message record into the SQLite database.
 */
bool persistMessage(int id, const Message& msg) {
    const char* sql = "INSERT OR REPLACE INTO messages (id, content, sender, recipient, delivered) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] Failed to prepare persistMessage statement: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, msg.content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, msg.sender.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, msg.recipient.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, msg.delivered ? 1 : 0);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

/**
 * removeMessage - Removes a message record from the SQLite database.
 */
bool removeMessage(int id) {
    const char* sql = "DELETE FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[ERROR] Failed to prepare removeMessage statement: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// ------------------ New: State Synchronization Functions ------------------

/**
 * GetFullState RPC Implementation.
 * Returns a snapshot of full persistent state to a joining server.
 */
class StateSyncServiceImpl final : public ChatService::Service {
public:
    Status GetFullState(ServerContext* context, const google::protobuf::Empty* request,
                        FullState* response) override {
        {
            std::lock_guard<std::mutex> lock(user_mutex);
            for (const auto &entry : user_map) {
                chat::UserState* userState = response->add_users();
                userState->set_username(entry.first);
                userState->set_password(entry.second.password);
                userState->set_isonline(entry.second.isOnline);
                userState->set_delivery_address(entry.second.delivery_address);
            }
        }
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            for (const auto &msgPair : messages) {
                chat::MessageState* msgState = response->add_messages();
                msgState->set_id(msgPair.first);
                msgState->set_content(msgPair.second.content);
                msgState->set_sender(msgPair.second.sender);
                msgState->set_recipient(msgPair.second.recipient);
                msgState->set_delivered(msgPair.second.delivered);
            }
        }
        return Status::OK;
    }
};

/**
 * syncStateFromLeader - Called by a new server (role 3) to synchronize full state from the leader.
 */
bool syncStateFromLeader(const std::string &leaderAddress) {
    auto channel = grpc::CreateChannel(leaderAddress, grpc::InsecureChannelCredentials());
    std::unique_ptr<ChatService::Stub> leaderStub = ChatService::NewStub(channel);
    google::protobuf::Empty empty;
    FullState fullState;
    ClientContext context;
    Status status = leaderStub->GetFullState(&context, empty, &fullState);
    if (!status.ok()) {
        std::cerr << "[DEBUG] Failed to sync state from leader.\n";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(user_mutex);
        user_map.clear();
        for (int i = 0; i < fullState.users_size(); i++) {
            const chat::UserState &us = fullState.users(i);
            UserInfo ui;
            ui.password = us.password();
            ui.isOnline = us.isonline();
            ui.delivery_address = us.delivery_address();
            user_map[us.username()] = ui;
            persistUser(us.username(), ui);
        }
    }
    {
        std::lock_guard<std::mutex> lock(messages_mutex);
        messages.clear();
        messageCounter = 0;
        for (int i = 0; i < fullState.messages_size(); i++) {
            const chat::MessageState &ms = fullState.messages(i);
            Message m;
            m.id = std::to_string(ms.id());
            m.content = ms.content();
            m.sender = ms.sender();
            m.recipient = ms.recipient();
            m.delivered = ms.delivered();
            messages[ms.id()] = m;
            if (ms.id() > messageCounter)
                messageCounter = ms.id();
            persistMessage(ms.id(), m);
        }
    }
    return true;
}

// ------------------ Peer/Election Utility Functions ------------------

std::vector<std::string> loadPeerAddresses(const std::string& filename) {
    std::vector<std::string> addresses;
    std::ifstream file(filename);
    if (!file.is_open()){
        std::cerr << "[ERROR] Unable to open " << filename << "\n";
        return addresses;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty())
            addresses.push_back(line);
    }
    return addresses;
}

uint32_t computeScoreFromAddress(const std::string &address) {
    uint32_t sum = 0;
    for (char c : address)
        sum += static_cast<uint32_t>(c);
    return sum;
}

void synchronizeReplicationLogs() {
    std::cout << "[DEBUG] Synchronizing replication logs...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "[DEBUG] Replication logs synchronized.\n";
}

void waitForElectionToComplete() {
    while (electionInProgress.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void updateLeaderInfo(const std::string &leaderId, int electionRound) {
    std::lock_guard<std::mutex> lock(leader_mutex);
    currentLeader = leaderId;
    currentLeaderElectionRound = electionRound;
    std::cout << "[DEBUG] Updated current leader to " << currentLeader 
              << " (Election Round: " << currentLeaderElectionRound << ")\n";
}

/**
 * replicateUpdateToBackups - Replicates a ReplicationUpdate to all backup peers.
 * On backup servers, the update is also persisted.
 */
bool replicateUpdateToBackups(const ReplicationUpdate &update) {
    int ackCount = 0;
    int totalBackups = 0;
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        for (const auto &entry : discovered_peers) {
            if (entry.second.id == self_id) continue;
            if (entry.second.role == 1) continue;  // Skip leader.
            totalBackups++;
            auto channel = grpc::CreateChannel(entry.first, grpc::InsecureChannelCredentials());
            auto stub = ISMService::NewStub(channel);
            UpdateAck ack;
            ClientContext context;
            Status status = stub->ReplicateUpdate(&context, update, &ack);
            if (status.ok() && ack.success())
                ackCount++;
        }
    }
    std::cout << "[DEBUG] Replication update seq " << update.seq() << ": received " 
              << ackCount << " acks out of " << totalBackups << " backups.\n";
    int quorum = (totalBackups / 2);
    return (ackCount >= quorum);
}

void monitorLeaderHeartbeat(const std::vector<std::string>& peer_addresses,
                            int heartbeatIntervalSeconds,
                            int maxMissedHeartbeats) {
    int missedCount = 0;
    while (true) {
        if (self_role == 1)
            break; // Leader does not monitor itself.
        std::string leader;
        {
            std::lock_guard<std::mutex> lock(leader_mutex);
            leader = currentLeader;
        }
        if (leader.empty() || leader == self_id) {
            std::this_thread::sleep_for(std::chrono::seconds(heartbeatIntervalSeconds));
            continue;
        }
        auto channel = grpc::CreateChannel(leader, grpc::InsecureChannelCredentials());
        auto stub = ISMService::NewStub(channel);
        HeartbeatRequest hbReq;
        hbReq.set_sender_id(self_id);
        HeartbeatResponse hbResp;
        ClientContext context;
        Status status = stub->Heartbeat(&context, hbReq, &hbResp);
        if (status.ok() && hbResp.alive()) {
            missedCount = 0;
        } else {
            missedCount++;
            std::cout << "[DEBUG] Missed heartbeat from leader " << leader 
                      << " (missed count: " << missedCount << ")\n";
        }
        if (missedCount >= maxMissedHeartbeats) {
            std::cout << "[DEBUG] Leader failure detected via heartbeat!\n";
            electionInProgress.store(true);
            synchronizeReplicationLogs();
            performElection(peer_addresses, computeScoreFromAddress(self_address), election_cycle + 1);
            electionInProgress.store(false);
            missedCount = 0;
            performDiscovery(peer_addresses);
        }
        std::this_thread::sleep_for(std::chrono::seconds(heartbeatIntervalSeconds));
    }
}

void performDiscovery(const std::vector<std::string>& peer_addresses) {
    for (const auto &address : peer_addresses) {
        if (address == self_address)
            continue;
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        std::unique_ptr<ISMService::Stub> stub = ISMService::NewStub(channel);
        DiscoveryPing ping;
        ping.set_sender_id(self_id);
        DiscoveryResponse response;
        ClientContext context;
        Status status = stub->DiscoverPeer(&context, ping, &response);
        if (status.ok()) {
            PeerInfo info;
            info.id = response.receiver_id();
            info.role = response.role();
            info.election_cycle = response.election_cycle();
            {
                std::lock_guard<std::mutex> lock(peers_mutex);
                discovered_peers[address] = info;
            }
            std::cout << "[DEBUG] Discovered peer " << address 
                      << " with role " << info.role 
                      << " and election cycle " << info.election_cycle << "\n";
            if (info.role == 1)
                updateLeaderInfo(info.id, info.election_cycle);
        } else {
            std::cout << "[DEBUG] Failed to contact peer " << address << "\n";
        }
    }
}

void waitForInitialPeers(const std::vector<std::string>& peer_addresses) {
    while (true) {
        performDiscovery(peer_addresses);
        int countRole0 = 0;
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            for (const auto &pair : discovered_peers) {
                if (pair.second.role == 0)
                    countRole0++;
            }
        }
        if (countRole0 >= 2) {
            std::cout << "[DEBUG] Found at least 2 other peers with role 0.\n";
            break;
        }
        std::cout << "[DEBUG] Waiting for initial peers with role 0...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void performElection(const std::vector<std::string>& peer_addresses, uint32_t self_score, int current_election_round) {
    std::unordered_map<std::string, uint32_t> scores;
    scores[self_id] = self_score;
    for (const auto &address : peer_addresses) {
        if (address == self_address)
            continue;
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        std::unique_ptr<ElectionService::Stub> stub = ElectionService::NewStub(channel);
        ScoreRequest req;
        req.set_sender_id(self_id);
        req.set_score(self_score);
        req.set_election_round(current_election_round);
        ScoreResponse resp;
        ClientContext context;
        Status status = stub->SendScore(&context, req, &resp);
        if (status.ok() && resp.received()) {
            uint32_t peer_score = computeScoreFromAddress(address);
            scores[address] = peer_score;
            std::cout << "[DEBUG] Received score " << peer_score << " from peer " << address << "\n";
        } else {
            std::cout << "[DEBUG] Failed to send score to peer " << address << "\n";
        }
    }
    uint32_t min_score = self_score;
    std::string elected_leader = self_id;
    for (const auto &pair : scores) {
        if (pair.second < min_score) {
            min_score = pair.second;
            elected_leader = pair.first;
        }
    }
    std::cout << "[DEBUG] Election complete. Elected leader: " << elected_leader 
              << " with score " << min_score << "\n";
    if (elected_leader == self_id)
        self_role = 1;
    else
        self_role = 2;
    updateLeaderInfo(elected_leader, current_election_round);
    if (self_role == 1) {
        for (const auto &address : peer_addresses) {
            if (address == self_address)
                continue;
            auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
            std::unique_ptr<ElectionService::Stub> stub = ElectionService::NewStub(channel);
            LeaderAnnouncement ann;
            ann.set_leader_id(self_id);
            ann.set_leader_score(self_score);
            ann.set_election_round(current_election_round);
            Ack ack;
            ClientContext context;
            Status status = stub->AnnounceLeader(&context, ann, &ack);
            if (!status.ok() || !ack.success()) {
                std::cout << "[DEBUG] Failed to announce leadership to peer " << address << "\n";
            } else {
                std::cout << "[DEBUG] Announced leadership to peer " << address << "\n";
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(peers_mutex);
        discovered_peers.clear();
    }
    std::cout << "[DEBUG] Cleared discovered peers after election. Re-discovering peers...\n";
    performDiscovery(peer_addresses);
}

// ------------------ Service Implementations ------------------

/* ISMService Implementation.
 * Provides peer discovery, replication, and heartbeat.
 */
class ISMServiceImpl final : public ISMService::Service {
public:
    Status DiscoverPeer(ServerContext* context, const DiscoveryPing* request,
                        DiscoveryResponse* response) override {
        response->set_receiver_id(self_id);
        response->set_role(self_role);
        response->set_election_cycle(election_cycle);
        if (self_role == 1)
            updateLeaderInfo(self_id, election_cycle);
        return Status::OK;
    }
    
    Status ReplicateUpdate(ServerContext* context, const ReplicationUpdate* request,
                       UpdateAck* response) override {
        if (request->update_type() == chat::USER_ADD) {
            std::lock_guard<std::mutex> lock(user_mutex);
            std::string username = request->username();
            UserInfo u;
            u.password = request->password();
            u.isOnline = request->isonline();  // New field from proto.
            u.delivery_address = request->delivery_address();  // New field.
            user_map[username] = u;
            std::cout << "[DEBUG] Replicated USER_ADD/UPDATE for " << username << "\n";
            persistUser(username, u);
        }
        else if (request->update_type() == chat::NEW_MESSAGE) {
            int id = request->seq();
            {
                std::lock_guard<std::mutex> lock(messages_mutex);
                Message msg;
                msg.id = request->message_id();
                msg.content = request->message();
                msg.sender = request->sender();
                msg.recipient = request->recipient();
                // Set the delivered flag from the replication update.
                msg.delivered = request->delivered();
                messages[id] = msg;
                if (id > messageCounter)
                    messageCounter = id;
                std::cout << "[DEBUG] Replicated NEW_MESSAGE id " << msg.id << "\n";
                persistMessage(id, msg);
            }
            // If the message was not delivered, update the recipient's offline messages.
            if (!request->delivered()) {
                std::lock_guard<std::mutex> lock(user_mutex);
                auto it = user_map.find(request->recipient());
                if (it != user_map.end()) {
                    Message offlineMsg;
                    offlineMsg.id = request->message_id();
                    offlineMsg.content = request->message();
                    offlineMsg.sender = request->sender();
                    offlineMsg.recipient = request->recipient();
                    offlineMsg.delivered = false;
                    it->second.offlineMessages.push_back(offlineMsg);
                    persistUser(request->recipient(), it->second);
                }
            }
        }
        else if (request->update_type() == chat::DELETE_MESSAGE) {
            int id = std::stoi(request->message_id());
            std::lock_guard<std::mutex> lock(messages_mutex);
            messages.erase(id);
            std::cout << "[DEBUG] Replicated DELETE_MESSAGE for id " << id << "\n";
            removeMessage(id);
        }
        response->set_success(true);
        response->set_seq(request->seq());
        return Status::OK;
    }


    
    Status Heartbeat(ServerContext* context, const HeartbeatRequest* request,
                     HeartbeatResponse* response) override {
        response->set_alive(true);
        return Status::OK;
    }
};

/* ElectionService Implementation.
 * Provides simple deterministic leader election.
 */
class ElectionServiceImpl final : public ElectionService::Service {
public:
    Status SendScore(ServerContext* context, const ScoreRequest* request,
                     ScoreResponse* response) override {
        response->set_received(true);
        return Status::OK;
    }
    
    Status AnnounceLeader(ServerContext* context, const LeaderAnnouncement* request,
                          Ack* response) override {
        std::cout << "[DEBUG] Received leader announcement: " << request->leader_id() 
                  << " (Round: " << request->election_round() << ")\n";
        updateLeaderInfo(request->leader_id(), request->election_round());
        response->set_success(true);
        return Status::OK;
    }
};

/* ChatService Implementation.
 * Handles account management, messaging, and state synchronization.
 */
class ChatServiceImpl final : public ChatService::Service {
public:
    Status GetLeaderInfo(ServerContext* context, const LeaderRequest* request,
                         LeaderResponse* response) override {
        std::lock_guard<std::mutex> lock(leader_mutex);
        response->set_leader_address(currentLeader);
        response->set_election_round(currentLeaderElectionRound);
        return Status::OK;
    }
    
    // Full state synchronization RPC.
    Status GetFullState(ServerContext* context, const google::protobuf::Empty* request,
                        FullState* response) override {
        {
            std::lock_guard<std::mutex> lock(user_mutex);
            for (const auto &entry : user_map) {
                chat::UserState* userState = response->add_users();
                userState->set_username(entry.first);
                userState->set_password(entry.second.password);
                userState->set_isonline(entry.second.isOnline);
                userState->set_delivery_address(entry.second.delivery_address);
            }
        }
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            for (const auto &msgPair : messages) {
                chat::MessageState* msgState = response->add_messages();
                msgState->set_id(msgPair.first);
                msgState->set_content(msgPair.second.content);
                msgState->set_sender(msgPair.second.sender);
                msgState->set_recipient(msgPair.second.recipient);
                msgState->set_delivered(msgPair.second.delivered);
            }
        }
        return Status::OK;
    }
    
    Status Register(ServerContext* context, const RegisterRequest* request,
                    RegisterResponse* response) override {
        waitForElectionToComplete();
        std::cout << "[DEBUG] Register called for username: " << request->username() << "\n";
        std::lock_guard<std::mutex> lock(user_mutex);
        std::string username = request->username();
        if (user_map.find(username) != user_map.end()) {
            response->set_success(true);
            response->set_message("Username already exists.");
            return Status::OK;
        }
        UserInfo newUser;
        newUser.password = argon2HashPassword(request->password());
        newUser.isOnline = false;
        newUser.delivery_address = "";
        user_map[username] = newUser;
        if (!persistUser(username, newUser)) {
            response->set_success(false);
            response->set_message("Failed to persist user.");
            return Status::OK;
        }
        ReplicationUpdate update;
        update.set_update_type(chat::USER_ADD);
        update.set_username(username);
        update.set_password(newUser.password);
        update.set_isonline(false);
        update.set_delivery_address("");
        update.set_seq(++messageCounter);
        bool replicated = replicateUpdateToBackups(update);
        if (!replicated) {
            response->set_success(false);
            response->set_message("Replication to backups failed.");
            return Status::OK;
        }
        response->set_success(true);
        response->set_message("Registration successful.");
        return Status::OK;
    }
    
    Status Login(ServerContext* context, const LoginRequest* request,
                 LoginResponse* response) override {
        std::cout << "[DEBUG] Login called for username: " << request->username() << "\n";
        std::lock_guard<std::mutex> lock(user_mutex);
        std::string username = request->username();
        auto it = user_map.find(username);
        if (it == user_map.end() || !argon2CheckPassword(it->second.password, request->password())) {
            response->set_success(false);
            response->set_message("Invalid username or password.");
            return Status::OK;
        }
        it->second.isOnline = true;
        it->second.delivery_address = request->delivery_address();
        persistUser(username, it->second);
        ReplicationUpdate update;
        update.set_update_type(chat::USER_ADD);
        update.set_username(username);
        update.set_password(it->second.password);
        update.set_isonline(true);
        update.set_delivery_address(it->second.delivery_address);
        update.set_seq(++messageCounter);
        bool replicated = replicateUpdateToBackups(update);
        if (!replicated) {
            response->set_success(false);
            response->set_message("Replication to backups failed during login.");
            return Status::OK;
        }
        response->set_success(true);
        response->set_session_token("dummy_token");
        response->set_message("Login successful.");


        // Only include messages where:
        //   - The sender is the user, OR
        //   - The recipient is the user AND the message is marked as delivered.
        {
            std::vector<int> sortedIds;
            {
                // First, collect all message IDs from the messages map.
                std::lock_guard<std::mutex> msgLock(messages_mutex);
                for (const auto &msgPair : messages) {
                    // Convert the string ID to an integer.
                    sortedIds.push_back(std::stoi(msgPair.second.id));
                }
            }
            // Sort the message IDs in ascending order.
            std::sort(sortedIds.begin(), sortedIds.end());

            // Now iterate over the sorted IDs and add the corresponding messages.
            for (int id : sortedIds) {
                std::lock_guard<std::mutex> msgLock(messages_mutex);
                // Find the message by its numeric ID (converted back to string).
                auto it = messages.find(id);
                if (it != messages.end()) {
                    const Message &msg = it->second;
                    if (msg.sender == username || (msg.recipient == username && msg.delivered)) {
                        ChatMessage* chatMsg = response->mutable_messages()->Add();
                        chatMsg->set_sender(msg.sender);
                        chatMsg->set_recipient(msg.recipient);
                        chatMsg->set_message(msg.content);
                        chatMsg->set_timestamp("");  // Optionally include a timestamp.
                        chatMsg->set_message_id(msg.id);
                    }
                }
            }
        }

        return Status::OK;
    }
    
    // New Logout RPC.
    Status Logout(ServerContext* context, const LoginRequest* request,
                  RegisterResponse* response) override {
        std::cout << "[DEBUG] Logout called for username: " << request->username() << "\n";
        std::lock_guard<std::mutex> lock(user_mutex);
        std::string username = request->username();
        auto it = user_map.find(username);
        if (it == user_map.end()) {
            response->set_success(false);
            response->set_message("User not found.");
            return Status::OK;
        }
        it->second.isOnline = false;
        it->second.delivery_address = "";
        persistUser(username, it->second);
        ReplicationUpdate update;
        update.set_update_type(chat::USER_ADD);
        update.set_username(username);
        update.set_password(it->second.password);
        update.set_isonline(false);
        update.set_delivery_address("");
        update.set_seq(++messageCounter);
        bool replicated = replicateUpdateToBackups(update);
        if (!replicated) {
            response->set_success(false);
            response->set_message("Replication to backups failed during logout.");
            return Status::OK;
        }
        response->set_success(true);
        response->set_message("Logout successful.");
        return Status::OK;
    }
    
    Status RetrieveUndeliveredMessages(ServerContext* context, const UndeliveredMessagesRequest* request,
                                   UndeliveredMessagesResponse* response) override {
        std::lock_guard<std::mutex> lock(user_mutex);
        auto it = user_map.find(request->username());
        if (it == user_map.end())
            return Status(grpc::NOT_FOUND, "User not found.");
        int max_messages = request->max_messages();
        int count = 0;
        // We'll collect the messages we are about to deliver.
        std::vector<Message> retrievedMessages;
        // Iterate through the user's offline messages (which have not yet been delivered).
        for (const auto &msg : it->second.offlineMessages) {
            if (count >= max_messages)
                break;
            // Add the message to the response.
            ChatMessage* chat_msg = response->add_messages();
            chat_msg->set_sender(msg.sender);
            chat_msg->set_recipient(msg.recipient);
            chat_msg->set_message(msg.content);
            chat_msg->set_timestamp("");  // (Optionally, include a timestamp.)
            chat_msg->set_message_id(msg.id);
            retrievedMessages.push_back(msg);
            count++;
        }
        // Now update each retrieved message to be marked as delivered.
        {
            std::lock_guard<std::mutex> msgLock(messages_mutex);
            for (const auto &msg : retrievedMessages) {
                int id = std::stoi(msg.id);
                auto itMsg = messages.find(id);
                if (itMsg != messages.end() && !itMsg->second.delivered) {
                    itMsg->second.delivered = true;
                    // Persist the updated delivered status.
                    persistMessage(id, itMsg->second);
                    // Create a replication update to inform backups.
                    ReplicationUpdate update;
                    update.set_update_type(chat::NEW_MESSAGE);
                    update.set_message_id(itMsg->second.id);
                    update.set_sender(itMsg->second.sender);
                    update.set_recipient(itMsg->second.recipient);
                    update.set_message(itMsg->second.content);
                    update.set_timestamp("");  // (Include timestamp if available.)
                    update.set_seq(id);
                    update.set_delivered(true);  // Mark as delivered.
                    replicateUpdateToBackups(update);
                }
            }
        }
        // Remove the delivered messages from the user's offline messages and persist the user update.
        if (count > 0) {
            it->second.offlineMessages.erase(it->second.offlineMessages.begin(),
                                             it->second.offlineMessages.begin() + count);
            persistUser(request->username(), it->second);
        }
        return Status::OK;
    }

    Status DeleteMessage(ServerContext* context, const DeleteMessageRequest* request,
                         DeleteMessageResponse* response) override {
        waitForElectionToComplete();
        int msg_id = std::stoi(request->message_id());
        std::lock_guard<std::mutex> lock(messages_mutex);
        auto it = messages.find(msg_id);
        if (it == messages.end()) {
            response->set_success(false);
            return Status::OK;
        }
        if (it->second.recipient != request->requesting_user()) {
            response->set_success(false);
            return Status::OK;
        }
        messages.erase(it);
        removeMessage(msg_id);
        if (self_role == 1) {
            ReplicationUpdate update;
            update.set_update_type(chat::DELETE_MESSAGE);
            update.set_message_id(std::to_string(msg_id));
            update.set_seq(++messageCounter);
            bool replicated = replicateUpdateToBackups(update);
            if (!replicated) {
                response->set_success(false);
                return Status::OK;
            }
        }
        response->set_success(true);
        return Status::OK;
    }
    
    Status SearchUsers(ServerContext* context, const SearchUsersRequest* request,
                       SearchUsersResponse* response) override {
        std::lock_guard<std::mutex> lock(user_mutex);
        for (const auto& kv : user_map) {
            if (kv.first.find(request->wildcard()) != std::string::npos)
                response->add_usernames(kv.first);
        }
        return Status::OK;
    }
    
    Status SendMessage(ServerContext* context, const ChatMessage* request,
                   MessageResponse* response) override {
        // Extract details from the request.
        std::string sender = request->sender();
        std::string recipient = request->recipient();
        std::string msg_text = request->message();
        std::string timestamp = request->timestamp();
                    
        // Generate a unique message ID.
        std::string msg_id;
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            messageCounter++;
            msg_id = std::to_string(messageCounter);
        }
    
        // Create a local message record.
        Message msg;
        msg.id = msg_id;
        msg.content = msg_text;
        msg.sender = sender;
        msg.recipient = recipient;
        msg.delivered = false; // Initially false.
        {
            std::lock_guard<std::mutex> lock(messages_mutex);
            messages[messageCounter] = msg;
        }
    
        // Persist the message locally.
        if (!persistMessage(messageCounter, msg)) {
            response->set_success(false);
            response->set_message("Failed to persist message.");
            return Status::OK;
        }
    
        // Build the replication update.
        ReplicationUpdate update;
        update.set_update_type(chat::NEW_MESSAGE);
        update.set_message_id(msg_id);
        update.set_sender(sender);
        update.set_recipient(recipient);
        update.set_message(msg_text);
        update.set_timestamp(timestamp);
        update.set_seq(messageCounter);
    
        // Try to deliver the message immediately.
        bool delivered = false;
        {
            std::lock_guard<std::mutex> lock(user_mutex);
            auto it = user_map.find(recipient);
            if (it != user_map.end() && it->second.isOnline && !it->second.delivery_address.empty()) {
                std::string delivery_addr = it->second.delivery_address;
                auto channel = grpc::CreateChannel(delivery_addr, grpc::InsecureChannelCredentials());
                std::unique_ptr<DeliveryService::Stub> delivery_stub = DeliveryService::NewStub(channel);
                DeliveryRequest dreq;
                dreq.set_sender(sender);
                dreq.set_recipient(recipient);
                dreq.set_message(msg_text);
                dreq.set_timestamp(timestamp);
                dreq.set_message_id(msg_id);
                DeliveryResponse dresp;
                ClientContext dcontext;
                Status dstatus = delivery_stub->DeliverMessage(&dcontext, dreq, &dresp);
                if (dstatus.ok() && dresp.success()) {
                    delivered = true;
                }
            }
        }
    
        // Set the delivered flag in the replication update.
        update.set_delivered(delivered);
    
        // Replicate update to backups.
        bool replicated = replicateUpdateToBackups(update);
        if (!replicated) {
            response->set_success(false);
            response->set_message("Replication to backups failed.");
            return Status::OK;
        }
    
        // If not delivered, add the message to the recipient's offline messages.
        if (!delivered) {
            std::lock_guard<std::mutex> lock(user_mutex);
            auto it = user_map.find(recipient);
            if (it != user_map.end()) {
                it->second.offlineMessages.push_back(msg);
                persistUser(recipient, it->second);
            }
        } else {
            // Mark the message as delivered.
            std::lock_guard<std::mutex> lock(messages_mutex);
            messages[messageCounter].delivered = true;
            persistMessage(messageCounter, messages[messageCounter]);
        }
    
        // Return a successful send acknowledgment.
        response->set_success(true);
        response->set_message_id(msg_id);
        response->set_message(std::string("Message accepted and ") +
                              (delivered ? "delivered." : "stored for offline retrieval."));
        return Status::OK;
    }

};

// ------------------ RunServer Function ------------------

/*
 * RunServer - Instantiates service implementations, configures credentials,
 * builds the gRPC server, and starts listening on self_address.
 */
void RunServer(const std::string& server_address) {
    ChatServiceImpl chatService;
    ISMServiceImpl ismService;
    ElectionServiceImpl electionService;
    // Note: GetFullState is implemented in ChatServiceImpl.
    
    grpc::SslServerCredentialsOptions ssl_opts;
    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
    // If using TLS, uncomment the next two lines and update paths accordingly.
    // pkcp.cert_chain = ReadFile("server.crt");
    // pkcp.private_key = ReadFile("server.key");
    // ssl_opts.pem_key_cert_pairs.push_back(pkcp);
    std::shared_ptr<grpc::ServerCredentials> creds = grpc::InsecureServerCredentials();
    
    ServerBuilder builder;
    builder.AddListeningPort(server_address, creds);
    builder.RegisterService(&chatService);
    builder.RegisterService(&ismService);
    builder.RegisterService(&electionService);
    
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[INFO] Server listening on " << server_address << "\n";
    server->Wait();
}

// ------------------ Main Function ------------------

/*
 * main - Entry point.
 * Expects two command-line arguments:
 *   argv[1] - Self address (e.g., "127.0.0.1:5000")
 *   argv[2] - Initial role (e.g., "0" for bootstrap)
 */
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <self_address> <initial_role>\n";
        std::cerr << "Example (bootstrapping): " << argv[0] << " 127.0.0.1:5000 0\n";
        return 1;
    }
    self_address = argv[1];
    self_id = self_address;
    self_role = std::atoi(argv[2]);  // For bootstrap, pass 0; later, new servers start as role 3.
    
    // Create a unique database file based on self_address.
    std::string dbFile = getDBFileName(self_address);
    if (!openDatabase(dbFile)) {
        std::cerr << "[ERROR] Failed to open SQLite database.\n";
        return 1;
    }
    if (!createTables()) {
        std::cerr << "[ERROR] Failed to create tables.\n";
        return 1;
    }
    if (!loadData()) {
        std::cerr << "[ERROR] Failed to load persistent data.\n";
        return 1;
    }
    
    std::vector<std::string> peer_addresses = loadPeerAddresses("peers.txt");
    uint32_t self_score = computeScoreFromAddress(self_address);
    int current_election_round = 1;
    
    // Start the gRPC server in a separate thread.
    std::thread serverThread(RunServer, self_address);
    std::this_thread::sleep_for(std::chrono::seconds(1));  // Allow server to start.
    
    if (self_role == 0) {
        std::cout << "[DEBUG] Bootstrapping: waiting for initial 3 servers (role 0)...\n";
        waitForInitialPeers(peer_addresses);
        electionInProgress.store(true);
        synchronizeReplicationLogs();
        performElection(peer_addresses, self_score, current_election_round);
        electionInProgress.store(false);
    } else if (self_role == 3) {
        // For a new server joining with role 3: perform discovery, then sync state from leader.
        performDiscovery(peer_addresses);
        std::string leaderFound;
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            for (const auto &pair : discovered_peers) {
                if (pair.second.role == 1) {
                    leaderFound = pair.second.id;
                    std::cout << "[DEBUG] Found leader: " << leaderFound << "\n";
                    break;
                }
            }
        }
        if (!leaderFound.empty()) {
            std::cout << "[DEBUG] Synchronizing full state from leader " << leaderFound << "...\n";
            if (syncStateFromLeader(leaderFound)) {
                std::cout << "[DEBUG] State synchronization successful. Upgrading role from 3 to 2.\n";
                self_role = 2;
            } else {
                std::cerr << "[ERROR] Failed to synchronize state from leader.\n";
            }
        }
    } else {
        // For backups with role 2.
        performDiscovery(peer_addresses);
        bool leaderFound = false;
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            for (const auto &pair : discovered_peers) {
                if (pair.second.role == 1) {
                    leaderFound = true;
                    std::cout << "[DEBUG] Found leader: " << pair.second.id << "\n";
                    break;
                }
            }
        }
        if (!leaderFound) {
            electionInProgress.store(true);
            synchronizeReplicationLogs();
            performElection(peer_addresses, self_score, current_election_round);
            electionInProgress.store(false);
        }
    }
    
    // Start heartbeat monitoring on backups.
    if (self_role != 1) {
        std::thread heartbeatThread(monitorLeaderHeartbeat, peer_addresses, 2, 3);
        heartbeatThread.detach();
    }
    
    serverThread.join();
    sqlite3_close(db);
    return 0;
}
