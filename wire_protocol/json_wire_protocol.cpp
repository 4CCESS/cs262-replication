// json_wire_protocol.cpp
#include "json_wire_protocol.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <unistd.h>
#include "json.hpp"  // Requires nlohmann::json

// Helper functions for SSL I/O

static ssize_t sendAllSSL(SSL* ssl, const uint8_t* data, size_t length) {
    size_t totalSent = 0;
    while (totalSent < length) {
        int sent = SSL_write(ssl, data + totalSent, length - totalSent);
        if (sent <= 0) {
            int err = SSL_get_error(ssl, sent);
            std::cerr << "[json_wire_protocol] SSL_write failed (error " << err << ")\n";
            return -1;
        }
        totalSent += sent;
    }
    return totalSent;
}

static ssize_t recvAllSSL(SSL* ssl, uint8_t* data, size_t length) {
    size_t totalReceived = 0;
    while (totalReceived < length) {
        int r = SSL_read(ssl, data + totalReceived, length - totalReceived);
        if (r <= 0) {
            int err = SSL_get_error(ssl, r);
            std::cerr << "[json_wire_protocol] SSL_read failed (error " << err << ")\n";
            return -1;
        }
        totalReceived += r;
    }
    return totalReceived;
}

// Serialize a Packet into MessagePack (via nlohmann::json)
std::vector<uint8_t> serializePacket(const Packet& pkt) {
    nlohmann::json j;
    j["op_code"]     = std::string(1, pkt.op_code);
    j["username"]    = pkt.username;
    j["password"]    = pkt.password;
    j["sender"]      = pkt.sender;
    j["recipient"]   = pkt.recipient;
    j["message"]     = pkt.message;
    j["message_id"]  = pkt.message_id;
    j["isValidated"] = pkt.isValidated;
    std::vector<uint8_t> msgpackData = nlohmann::json::to_msgpack(j);
    return msgpackData;
}

// Deserialize MessagePack data into a Packet
std::unique_ptr<Packet> deserializePacket(const std::vector<uint8_t>& data) {
    nlohmann::json j = nlohmann::json::from_msgpack(data);
    auto pkt = std::make_unique<Packet>();
    std::string opStr = j.value("op_code", "");
    if (!opStr.empty())
        pkt->op_code = opStr[0];
    pkt->username    = j.value("username", "");
    pkt->password    = j.value("password", "");
    pkt->sender      = j.value("sender", "");
    pkt->recipient   = j.value("recipient", "");
    pkt->message     = j.value("message", "");
    pkt->message_id  = j.value("message_id", "");
    pkt->isValidated = j.value("isValidated", false);
    return pkt;
}

// Send a Packet over an SSL connection using a 4-byte length prefix.
int sendPacketSSL(SSL* ssl, const Packet& pkt) {
    try {
        std::vector<uint8_t> data = serializePacket(pkt);
        uint32_t netSize = htonl(static_cast<uint32_t>(data.size()));
        if (sendAllSSL(ssl, reinterpret_cast<const uint8_t*>(&netSize), sizeof(netSize)) != sizeof(netSize))
            return FAILURE;
        if (sendAllSSL(ssl, data.data(), data.size()) != static_cast<ssize_t>(data.size()))
            return FAILURE;
        return SUCCESS;
    } catch (const std::exception &e) {
        std::cerr << "[json_wire_protocol] Exception in sendPacketSSL: " << e.what() << "\n";
        return FAILURE;
    }
}

// Receive a Packet over an SSL connection.
std::unique_ptr<Packet> receivePacketSSL(SSL* ssl) {
    uint32_t netSize = 0;
    if (recvAllSSL(ssl, reinterpret_cast<uint8_t*>(&netSize), sizeof(netSize)) != sizeof(netSize))
        return nullptr;
    uint32_t dataSize = ntohl(netSize);
    if (dataSize == 0)
        return nullptr;
    std::vector<uint8_t> buffer(dataSize);
    if (recvAllSSL(ssl, buffer.data(), dataSize) != static_cast<ssize_t>(dataSize))
        return nullptr;
    try {
        return deserializePacket(buffer);
    } catch (const std::exception &e) {
        std::cerr << "[json_wire_protocol] Exception in receivePacketSSL: " << e.what() << "\n";
        return nullptr;
    }
}
