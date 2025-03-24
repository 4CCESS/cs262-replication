// custom_wire_protocol.cpp
#include "custom_wire_protocol.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <unistd.h>

// Helper functions for SSL I/O

static ssize_t sendAllSSL(SSL* ssl, const uint8_t* data, size_t length) {
    size_t totalSent = 0;
    while (totalSent < length) {
        int sent = SSL_write(ssl, data + totalSent, length - totalSent);
        if (sent <= 0) {
            int err = SSL_get_error(ssl, sent);
            std::cerr << "[custom_wire_protocol] SSL_write failed (error " << err << ")\n";
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
            std::cerr << "[custom_wire_protocol] SSL_read failed (error " << err << ")\n";
            return -1;
        }
        totalReceived += r;
    }
    return totalReceived;
}

// Serialize a Packet into a delimiter-separated string.
std::vector<uint8_t> serializePacket(const Packet& pkt) {
    std::ostringstream oss;
    oss << "op_code:" << pkt.op_code << ";";
    oss << "username:" << pkt.username << ";";
    oss << "password:" << pkt.password << ";";
    oss << "sender:" << pkt.sender << ";";
    oss << "recipient:" << pkt.recipient << ";";
    oss << "message:" << pkt.message << ";";
    oss << "message_id:" << pkt.message_id << ";";
    oss << "isValidated:" << (pkt.isValidated ? "1" : "0") << ";";
    std::string str = oss.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

// Deserialize a delimiter-separated string into a Packet.
std::unique_ptr<Packet> deserializePacket(const std::vector<uint8_t>& data) {
    auto pkt = std::make_unique<Packet>();
    std::string str(data.begin(), data.end());
    std::istringstream iss(str);
    std::string token;
    while (std::getline(iss, token, ';')) {
        if (token.empty())
            continue;
        size_t pos = token.find(':');
        if (pos == std::string::npos)
            continue;
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1);
        if (key == "op_code") {
            if (!value.empty())
                pkt->op_code = value[0];
        } else if (key == "username") {
            pkt->username = value;
        } else if (key == "password") {
            pkt->password = value;
        } else if (key == "sender") {
            pkt->sender = value;
        } else if (key == "recipient") {
            pkt->recipient = value;
        } else if (key == "message") {
            pkt->message = value;
        } else if (key == "message_id") {
            pkt->message_id = value;
        } else if (key == "isValidated") {
            pkt->isValidated = (value == "1");
        }
    }
    return pkt;
}

// Send a Packet over an SSL connection with a 4-byte size header.
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
        std::cerr << "[custom_wire_protocol] Exception in sendPacketSSL: " << e.what() << "\n";
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
        std::cerr << "[custom_wire_protocol] Exception in receivePacketSSL: " << e.what() << "\n";
        return nullptr;
    }
}
