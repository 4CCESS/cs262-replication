#ifndef PACKET_H
#define PACKET_H

#include <string>

/**
 * @brief A single Packet class that holds all possible fields.
 *
 * The 'op_code' tells us which operation is intended:
 *   - 'L' => login (use username, password)
 *   - 'R' => register (use username, password)
 *   - 's' => send message (use sender, recipient, message)
 *   - 'd' => delete message (use sender, message_id)
 *   - 'l' => list users (use sender)
 *   - 'q' => quit (no fields needed)
 *   - 'v' => validate (use isValidated, optional error messages)
 *   - 'm' => custom operation code
 *
 * Unused fields default to "" or false, so you only fill what you need.
 */
class Packet {
public:
    // Packet fields
    char op_code;           // e.g., 'L', 'R', 's', 'd', 'l', 'q', 'v', 'm', ...
    std::string username;
    std::string password;
    std::string sender;
    std::string recipient;
    std::string message;
    std::string message_id;
    bool isValidated;

    /**
     * @brief Default constructor sets op_code to '?' and everything else to defaults.
     */
    Packet();

};

#endif // PACKET_H
