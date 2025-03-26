Below is a complete README.md file in plain text Markdown. You can copy and paste this directly into a file named `README.md`.

---

```markdown
# Distributed Chat Application

This project is a distributed chat system built using gRPC and SQLite for persistence. The system consists of one or more servers that support leader election, peer discovery, message replication, and persistence, and a client (with a Tkinter GUI) that connects to the primary server to perform user authentication, message sending/receiving, offline message retrieval, and user searches.

## Table of Contents

- [Features](#features)
- [Dependencies](#dependencies)
- [Installation](#installation)
- [Compilation and Build](#compilation-and-build)
- [Usage](#usage)
  - [Running the Server](#running-the-server)
  - [Running the Client & GUI](#running-the-client--gui)
- [Architecture and Method Overviews](#architecture-and-method-overviews)
  - [Server Components](#server-components)
  - [Client Components](#client-components)
  - [GUI Overview](#gui-overview)
- [Replication and Persistence](#replication-and-persistence)
- [State Synchronization](#state-synchronization)
- [Unit Testing](#unit-testing)
- [Troubleshooting and Logs](#troubleshooting-and-logs)
- [License](#license)

## Features

- **Leader Election & Peer Discovery**  
  Servers discover each other and perform a deterministic leader election based on a computed score from their address.

- **Message Replication & Persistence**  
  The leader persists all chat messages and user data in an SQLite database. Replication updates are sent to backup servers so that all servers hold the same persistent state.

- **User Authentication**  
  Clients can register, log in, and log out. Login requests include the client’s delivery address so that the server can perform immediate message delivery if the user is online.

- **Message Delivery and Offline Message Retrieval**  
  Messages sent by users are delivered immediately if the recipient is online (using a dedicated DeliveryService). Otherwise, undelivered messages remain stored and can later be retrieved by the user.

- **State Synchronization**  
  New servers joining the network (starting with role 3) download the full persistent state from the leader before upgrading to a backup (role 2).

- **GUI Client**  
  A Python Tkinter-based GUI allows users to log in, register, send/receive messages, search users, retrieve offline messages, and manage their account.

## Dependencies

Ensure the following dependencies are installed before building the project:

- **gRPC & Protocol Buffers**  
  [gRPC C++](https://grpc.io/docs/languages/cpp/quickstart/) and [Protobuf](https://developers.google.com/protocol-buffers)  
  *Tip:* Set `CMAKE_PREFIX_PATH` to the directory where these libraries are installed.

- **SQLite3**  
  Install the SQLite3 development package (e.g., on Ubuntu: `sudo apt-get install libsqlite3-dev`).

- **CMake** (version 3.16 or higher)

- **A C++17 Compiler** (e.g., g++ 7+)

- **Python 3** (for the GUI client)

- **Tkinter** (usually included with Python; if not, install it via your package manager)

- **GoogleTest (gtest)**  
  Install via your package manager (e.g., on Ubuntu: `sudo apt-get install libgtest-dev`) or build from source.

## Installation

1. **Clone the Repository:**

   ```bash
   git clone https://github.com/yourusername/distributed-chat.git
   cd distributed-chat
   ```

2. **Install gRPC & Protobuf:**  
   Follow the [gRPC C++ installation guide](https://grpc.io/docs/languages/cpp/quickstart/) to install these libraries.

3. **Install SQLite3:**  
   On Debian/Ubuntu:

   ```bash
   sudo apt-get install libsqlite3-dev
   ```

4. **Install Python Dependencies for the GUI:**  
   Ensure Tkinter is installed (on Ubuntu, you might use `sudo apt-get install python3-tk`).

5. **Install GoogleTest (if running unit tests):**  
   On Ubuntu, you may install with:

   ```bash
   sudo apt-get install libgtest-dev
   cd /usr/src/gtest
   sudo cmake .
   sudo make
   sudo cp *.a /usr/lib
   ```

## Compilation and Build

1. **Create a Build Directory:**

   ```bash
   mkdir build
   cd build
   ```

2. **Configure the Project with CMake:**

   ```bash
   cmake ..
   ```

   This generates the build files for the server, client, and unit tests.

3. **Build the Project:**

   ```bash
   cmake --build . --target server
   cmake --build . --target client
   cmake --build . --target test_chat
   ```

   This builds the server (`server`), client (`client`), and the test executable (`test_chat`).

## Usage

### Running the Server

Run the server with the following command-line arguments:

- `<self_address>`: The IP address and port for this server (e.g., `127.0.0.1:5000`).
- `<initial_role>`:  
  - Use `0` for bootstrapping the first server.  
  - Use `3` for new servers joining the network.

Example:

```bash
./server 127.0.0.1:5000 0
```

Each server creates its own unique SQLite database (e.g., `chat_127.0.0.1_5000.db`) to persist user and message data.

### Running the Client & GUI

The client executable automatically reads the list of servers from `servers.txt` and connects to the primary server.  
The Python GUI client is launched with a single argument: the client’s delivery address (e.g., `192.168.1.100:6000`).

Example:

```bash
python3 gui.py 192.168.1.100:6000
```

## Architecture and Method Overviews

### Server Components

- **ISMService**  
  - *DiscoverPeer*: Responds to discovery pings with server ID, role, and election cycle.  
  - *ReplicateUpdate*: Applies replication updates for user creation/updates (including online status and delivery address), new messages (with delivered flag), and message deletions. Updates are persisted to SQLite and in-memory, then replicated to backups.  
  - *Heartbeat*: Responds to heartbeat checks from backup servers.

- **ElectionService**  
  - *SendScore*: Receives scores from peers during leader election.  
  - *AnnounceLeader*: Processes leader announcements and updates global leader state.

- **ChatService**  
  - *Register*: Registers a new user, persists the record, and replicates the update to backups.  
  - *Login*: Authenticates a user, updates their online status and delivery address, persists these changes, replicates them, and returns a login response along with a list of messages that have been delivered (i.e. those sent by or delivered to the user).  
  - *Logout*: Marks the user as offline, clears their delivery address, persists and replicates the update.  
  - *RetrieveUndeliveredMessages*: Returns offline messages to the user. Upon retrieval, messages are marked as delivered in both memory and persistent storage, and the update is replicated to backups.  
  - *DeleteMessage*: Deletes a message and replicates the deletion.  
  - *SearchUsers*: Returns a list of users matching a search term.  
  - *SendMessage*: Generates a unique message ID, creates and persists a message record, attempts immediate delivery via DeliveryService, and replicates the update. If delivery fails, the message is stored as an offline message and marked accordingly.  
  - *GetFullState*: Provides full state (user and message data) for state synchronization with new servers.

- **State Synchronization**  
  - New servers (role 3) call *GetFullState* on the leader to download the complete persistent state, then populate their in-memory data and SQLite database before upgrading to role 2.

- **SQLite Persistence Functions**  
  - *persistUser*, *persistMessage*, *removeMessage*: Update the SQLite database with user and message changes.  
  - *loadData*: Loads persistent data on startup.

### Client Components

- **ChatService Stub**  
  Used for operations such as login, registration, sending messages, retrieving messages, deleting messages, and searching users.

- **DeliveryService**  
  A local gRPC service that runs within the client process to handle immediate message deliveries from the server. Delivered messages are forwarded (as JSON) to the GUI process queue.

- **Operation Helpers with Retry Logic**  
  Each operation (login, registration, sending, retrieving, listing, deleting) is retried up to 3 times. If all attempts fail, the client re-discovers the primary server and retries the same command.

### GUI Overview

- **Tkinter-based GUI Client**  
  Provides separate views for authentication and the main chat interface.  
  - *Login Window*: Prompts for username, password, and mode (login or register).  
  - *Main Window*: Contains panels for user search, displaying chat messages, retrieving offline messages, sending messages, and account management (logout, delete account).  
- **Communication with the Client Process**  
  The GUI launches the client as a subprocess, sends commands via STDIN, and receives JSON responses via STDOUT, which are parsed and displayed accordingly.

## Replication and Persistence

- **Persistence**:  
  Each server stores user and message data in its unique SQLite database file.  
  Operations such as registration, login, sending, and message deletion update the persistent storage and are then replicated to backup servers.

- **Replication**:  
  The leader sends replication updates via the `ReplicateUpdate` RPC. These updates include user data (with online status and delivery address), new messages (with delivered flags), and message deletions. Backups update their in-memory state and persist changes to SQLite.

- **Offline Message Handling**:  
  Undelivered messages are stored as part of the user record. When retrieved, messages are marked as delivered in memory and in the SQLite database, and these changes are replicated to backups.

## State Synchronization

- **New Server Sync**:  
  When a new server (role 3) joins the network, it calls *GetFullState* on the leader to download the complete persistent state (user records and messages) and populates its in-memory state and SQLite database before upgrading to backup (role 2).

## Unit Testing

The project includes unit tests for both client and server components using GoogleTest.

### Build and Run Tests

1. **Configure Tests with CMake:**

   From your build directory, run:
   ```bash
   cmake ..
   ```

2. **Build the Test Executable:**

   ```bash
   cmake --build . --target test_chat
   ```

3. **Run the Tests:**

   Either using CTest:
   ```bash
   ctest --verbose
   ```
   Or by running the test binary directly:
   ```bash
   ./test_chat
   ```

Check the test output for any failures or errors.

## Troubleshooting and Logs

- **Logging**:  
  Look for `[DEBUG]`, `[ERROR]`, and `[INFO]` messages in the console output to trace the application flow.

- **Common Issues**:  
  - *Replication Failures*: Verify that all server addresses in `peers.txt` are correct and reachable.  
  - *SQLite Persistence Errors*: Confirm that SQLite3 is installed and the server has permission to write to its database file.  
  - *State Synchronization Problems*: Ensure that the leader is reachable and that its full state is transmitted correctly to new servers.


---

This README provides comprehensive instructions on installing dependencies, building the project, running both the server and client (with GUI), an overview of the architecture, replication/persistence details, state synchronization, unit testing, troubleshooting, and licensing.
