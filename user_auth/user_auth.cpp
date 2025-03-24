#include <string> 
#include <cstdlib>
#include <cstdio>
#include <iostream>







#include "argon2.h" 
#include <openssl/rand.h>


// Argon2 parameters
static const uint32_t T_COST = 2;          // number of iterations
static const uint32_t M_COST = (1 << 16);  // memory usage (64MB)
static const uint32_t PARALLELISM = 1;
static const size_t SALT_LEN = 16;
static const size_t HASH_LEN = 32;
static const size_t ENCODED_LEN = 128;


/**
 * @brief Generate an Argon2 hash for a plaintext password.
 * @param password The user's plaintext password
 * @return The Argon2-encoded hash string, or "" on error
 */
std::string argon2HashPassword(const std::string &password) {
   // Generate random salt
   unsigned char salt[SALT_LEN];
   if (RAND_bytes(salt, SALT_LEN) != 1) {
       std::cerr << "[ERROR] RAND_bytes failed to generate salt.\n";
       return "";
   }
   char encoded[ENCODED_LEN];
   int ret = argon2_hash(
       T_COST, M_COST, PARALLELISM,
       password.data(), password.size(),
       salt, SALT_LEN,
       nullptr, HASH_LEN,
       encoded, ENCODED_LEN,
       Argon2_id, ARGON2_VERSION_13
   );
   if (ret != ARGON2_OK) {
       std::cerr << "[ERROR] argon2_hash: " << argon2_error_message(ret) << std::endl;
       return "";
   }
   return std::string(encoded);

}



/**
 * @brief Verify a plaintext password against an Argon2-encoded hash.
 * @param encodedHash The Argon2 encoded hash stored in userMap
 * @param password The plaintext password
 * @return true if correct, false otherwise
 */
bool argon2CheckPassword(const std::string &encodedHash, const std::string &password) {
    int ret = argon2_verify(encodedHash.c_str(), password.data(), password.size(), Argon2_id);
    return (ret == ARGON2_OK);
}






