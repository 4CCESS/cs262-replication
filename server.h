#ifndef SERVER_H
#define SERVER_H

#include <iostream>         
#include <thread>          
#include <unordered_map> 
#include <vector>      
#include <mutex>    
#include <string>          
#include <cstring>       
#include <cerrno>        
#include <cstdlib>     
#include <algorithm>    
#include <netinet/in.h>  
#include <arpa/inet.h> 
#include <unistd.h>
#include <netdb.h> 
#include <sys/types.h>  
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"

#include "user_auth/user_auth.h"
#include "wire_protocol/packet.h"
#include "wire_protocol/json_wire_protocol.h"           // (UN)COMMENT TO CHANGE PROTOCOLS.
// #include "wire_protocol/custom_wire_protocol.h"      // (UN)COMMENT TO CHANGE PROTOCOLS.


// Structure about messages exchanged by users. 
struct Message {
    std::string id;         // Unique message identifier as string
    std::string content;    // The actual message content
    std::string sender;     // Sender's username
    std::string recipient;  // Recipient's username
};

// User information. 
struct UserInfo {
    std::string password;      // Hashed password (using argon2)
    bool isOnline = false;     // Online status flag (false by default)
    int socketFd = -1;         // File descriptor for the user's socket
    SSL* ssl = nullptr;        // Pointer to the user's SSL connection
    std::vector<Message> offlineMessages;  // Unread, offline messages 
};


// Global message counter for assigning unique IDs to messages.
static int messageCounter = 0;

// Global container to store all messages, mapped by a unique integer ID.
static std::unordered_map<int, Message> messages;

// Mutex to protect access to the messages map in multi-threaded environment.
static std::mutex messagesMutex;

// Global container to store user information, mapped by username.
static std::unordered_map<std::string, UserInfo> userMap;

// Mutex to protect access to the userMap.
static std::mutex userMapMutex;


/**
 * @brief Initializes the OpenSSL context and loads the server's certificate and private key.
 * 
 * @return SSL_CTX* A pointer to the initialized SSL context.
 */
SSL_CTX* initializeSSLContext();


/**
 * @brief Handles user login over an SSL connection.
 * 
 * Repeatedly receives login attempts until a valid login is achieved or the client disconnects.
 * Also supports switching to user registration if requested.
 *
 * First, it checks that `initialUsername` already exists by finding in `userMap`. 
 * Proceeds to verify password using `argon2CheckPassword` (see user_auth/user_auth.h). 
 *
 * Sends a Packet object to client with field `isValidated` accordingly. 
 *
 * If logging in fails, expects another Packet indefinetely. 
 *  
 * @param ssl Pointer to the SSL connection.
 * @param initialUsername The initial username provided by the client.
 * @param initialPassword The initial password provided by the client.
 *
 * @return true if login is successful; false otherwise.
 */
bool userLogin(SSL* ssl, const std::string &initialUsername, const std::string &initialPassword);


/**
 * @brief Handles new user registration over an SSL connection.
 * 
 * Repeatedly receives registration attempts until a new account is successfully created,
 * or the client switches to login.
 * 
 * First, it checks that initialUsername is available by querying `userMap`. 
 * Encodes password using `argon2HashPassword` (see user_auth/user_auth.h). 
 *
 * If unsuccessful, expects another Packet indefinitely. 
 * 
 * @param ssl Pointer to the SSL connection.
 * @param initialUsername The initial username provided by the client.
 * @param initialPassword The initial password provided by the client.
 *
 * @return true if registration is successful; false otherwise.
 */
bool userRegister(SSL* ssl, const std::string &initialUsername, const std::string &initialPassword);


/**
 * @brief Handles communication with a connected client over an SSL connection.
 * 
 * This function processes different types of operations (op_codes) sent by the client,
 * including sending messages, retrieving offline messages, viewing message history,
 * listing users, deleting messages or accounts, and quitting the connection.
 *
 * Whenever this function is called in a thread created in main, 
 * a fresh new connection is established between the server and the client. 
 * Thus, a client will never be authenticated by the start of this function. 
 * Thus, the first Packet the server expects is one with username and password 
 * fields not empty so that the client can be authenticated. 
 *
 * Functions `userRegister` and `userLogin` are implemented to authenticate the user. 
 *
 * `Packet` objects are expected repeatedly until a `Packet` object with `op_code = 'q'` (quit)
 * is received. Each Packet is managed accordingly in this function. 
 *
 * Perhaps a better implementation would require extra modularity. 
 * 
 * @param ssl Pointer to the client's SSL connection.
 */
void handleClient(SSL* ssl);


#endif
