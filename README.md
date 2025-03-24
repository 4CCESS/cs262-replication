# Chat Application

This project implements a secure chat application using SSL and a custom JSON wire protocol. It consists of two main components:

- **server.cpp**  
  Implements a multi-threaded SSL server that:
  - Initializes an SSL context (loading the certificate and private key).
  - Accepts incoming SSL connections.
  - Handles user authentication (login/registration) using Argon2 for password hashing.
  - Processes various client requests:
    - Sending messages (both direct and offline),
    - Retrieving offline messages,
    - Requesting chat history,
    - Listing users,
    - Deleting messages,
    - Deleting user accounts,
    - And quitting.
  - Uses global containers (protected by mutexes) to maintain user information and message history.

  - To use it, install openssl@3 running the following on your terminal: `brew install openssl@3`. We'll also need to install argon2 (for password hashing) also using homebrew: `brew install argon2`.
  - Compile: 
  ```bash
  g++ -std=c++17 -pthread server.cpp wire_protocol/json_wire_protocol.cpp wire_protocol/packet.cpp user_auth/user_auth.cpp \
    -I<path to>argon2/include \
    -I<path to>openssl@3/include \
    -L<path to>argon2/lib \
    -L<path to>openssl@3/lib \
    -largon2 -lssl -lcrypto \
    -o server`
    On MACX, the compile command is usually: 
    ```bash
    g++ -std=c++17 -pthread server.cpp wire_protocol/json_wire_protocol.cpp wire_protocol/packet.cpp user_auth/user_auth.cpp \
    -I/opt/homebrew/opt/argon2/include \
    -I/opt/homebrew/opt/openssl@3/include \
    -L/opt/homebrew/opt/argon2/lib \
    -L/opt/homebrew/opt/openssl@3/lib \
    -largon2 -lssl -lcrypto \
    -o server'

  - Run: `./server <port_number>`. If no port number is passed, port 54000 is used by default.  

- **client.cpp**  
  Implements a command-line client (with an optional Tkinter-based GUI) that:
  - Connects to the server via SSL.
  - Sends commands (using a special delimiter between fields) for login/registration, messaging, retrieving messages/history, user search, deletion, and quitting.
  - Runs a background listener thread to process incoming packets from the server and update the GUI accordingly.

  - To use it, install openssl@3 running the following on your terminal: `brew install openssl@3`. We'll also need to install argon2 (for password hashing) also using homebrew: `brew install argon2`. Moreover, you'll need to install Tkinter to run the GUI. You do it by using the Python package manager: `pip install tk`. 
  - Compile: 
  ```bash
  g++ -std=c++17 -pthread client.cpp wire_protocol/json_wire_protocol.cpp wire_protocol/packet.cpp \
    -I<path to>argon2/include \
    -I<path to>openssl@3/include \
    -L<path to>argon2/lib \
    -L<path to>openssl@3/lib \
    -largon2 -lssl -lcrypto \
    -o client` 
    On MACX, the compile command is usually: 
    ```bash
    g++ -std=c++17 -pthread client.cpp wire_protocol/json_wire_protocol.cpp wire_protocol/packet.cpp \
    -I/opt/homebrew/opt/argon2/include \
    -I/opt/homebrew/opt/openssl@3/include \
    -L/opt/homebrew/opt/argon2/lib \
    -L/opt/homebrew/opt/openssl@3/lib \
    -largon2 -lssl -lcrypto \
    -o client

  - Run: `python3 client.py <server IP address> <port number>`.  

## How It Works Together

1. **Startup:**  
   The server initializes SSL and listens on a specified port. Clients launch (either via the command line or the provided GUI) and establish an SSL connection to the server.

2. **Authentication:**  
   Clients send login or registration commands. The server uses Argon2 to hash or verify passwords and sends back a validation packet.  
   On success, the client’s GUI switches from the login window to the main chat interface.

3. **Messaging:**  
   Once authenticated, clients can send messages to other users. The server:
   - Immediately forwards messages if the recipient is online,
   - Otherwise stores messages as “offline” messages.
   Clients can also request retrieval of offline messages, view their chat history, or delete messages.

4. **User List & Account Management:**  
   Clients may search for users, request a list, delete specific messages, or even delete their own account.

# Chat Application Tests

This project contains unit tests for both the server and client components of the chat application using GoogleTest.

## Prerequisites

- **C++17** compiler (e.g. g++)
- [GoogleTest](https://github.com/google/googletest) installed
- OpenSSL development libraries

## Building the Tests

Two separate test binaries are provided:

1. **Server Tests**  
   Build with:
   ```bash
   g++ -std=c++17 -pthread \
    -I/opt/homebrew/opt/googletest/include \
    -I/opt/homebrew/opt/openssl@3/include \
    -I./wire_protocol \
    -L/opt/homebrew/opt/googletest/lib \
    -L/opt/homebrew/opt/openssl@3/lib \
    server_test.cpp wire_protocol/json_wire_protocol.cpp wire_protocol/packet.cpp \
    -lgtest -lgtest_main -lssl -lcrypto -o server_test

2. **Client Tests** 
    Build with: 
    ```bash 
    g++ -std=c++17 -pthread \
    -I/opt/homebrew/opt/googletest/include \
    -I/opt/homebrew/opt/openssl@3/include \
    -I./wire_protocol \
    -L/opt/homebrew/opt/googletest/lib \
    -L/opt/homebrew/opt/openssl@3/lib \
    client_test.cpp wire_protocol/json_wire_protocol.cpp wire_protocol/packet.cpp \
    -lgtest -lgtest_main -lssl -lcrypto -o client_test
