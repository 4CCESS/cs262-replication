#ifndef USER_AUTH_H
#define USER_AUTH_H

#include <string>
#include <unordered_map>
#include <mutex>

// If needed, you can declare your global user map and mutex here.
// For example:
// struct UserInfo {
//     std::string password; // Argon2-encoded hash
//     bool isOnline;
//     int socketFd;
// };
// extern std::unordered_map<std::string, UserInfo> userMap;
// extern std::mutex userMapMutex;

/**
 * @brief Generate an Argon2 hash for a plaintext password.
 * @param password The user's plaintext password.
 * @return The Argon2-encoded hash string, or an empty string on error.
 */
std::string argon2HashPassword(const std::string &password);

/**
 * @brief Verify a plaintext password against an Argon2-encoded hash.
 * @param encodedHash The stored Argon2-encoded hash.
 * @param password The plaintext password.
 * @return True if the password is correct, false otherwise.
 */
bool argon2CheckPassword(const std::string &encodedHash, const std::string &password);

// Optionally, you could declare a loginOrRegister function if you wish.
// bool loginOrRegister(int sockFd, const std::string &username, const std::string &password, const std::string &op);

#endif // USER_AUTH_H
