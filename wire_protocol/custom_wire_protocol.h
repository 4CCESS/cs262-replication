#ifndef CUSTOM_WIRE_PROTOCOL_H
#define CUSTOM_WIRE_PROTOCOL_H

#include <vector>
#include <memory>
#include <cstdint>
#include <string>
#include "packet.h"       // Include the Packet class definition
#include <openssl/ssl.h>  // For SSL*

// Return codes
constexpr int SUCCESS = 0;
constexpr int FAILURE = -1;

/**
 * @brief Serialize a Packet to a binary buffer using a custom delimiter-separated protocol.
 *        Format: key1:value1;key2:value2;...;keyN:valueN;
 * @param pkt A reference to the Packet object.
 * @return A std::vector<uint8_t> containing the serialized data.
 */
std::vector<uint8_t> serializePacket(const Packet& pkt);

/**
 * @brief Deserialize a binary buffer into a Packet object using the custom protocol.
 *        Expects format: key1:value1;key2:value2;...;keyN:valueN;
 * @param data A std::vector<uint8_t> containing serialized data.
 * @return A std::unique_ptr<Packet> containing the deserialized Packet.
 */
std::unique_ptr<Packet> deserializePacket(const std::vector<uint8_t>& data);

/**
 * @brief Send a Packet over an SSL connection with a 4-byte size prefix using the custom protocol.
 * @param ssl The OpenSSL SSL pointer for the connection.
 * @param pkt The Packet to send.
 * @return SUCCESS (0) on success, FAILURE (-1) on error.
 */
int sendPacketSSL(SSL* ssl, const Packet& pkt);

/**
 * @brief Receive a Packet from an SSL connection with a 4-byte size prefix using the custom protocol.
 * @param ssl The OpenSSL SSL pointer for the connection.
 * @return A std::unique_ptr<Packet> on success, or nullptr on error.
 */
std::unique_ptr<Packet> receivePacketSSL(SSL* ssl);

#endif // CUSTOM_WIRE_PROTOCOL_H
