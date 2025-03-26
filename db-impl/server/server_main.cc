#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "server.h"
#include "db_manager.h"

// ---------------------------------------------------------------
// server_main.cc
//
// This file contains the main() function that starts the gRPC server.
// It performs the following tasks:
// 1. Sets the server's address and port.
// 2. Initializes the SQLite database via DBManager.
// 3. Creates an instance of ChatServer to handle client requests.
// 4. Configures and starts the gRPC server to listen for incoming RPC calls.
// 5. Blocks execution until the server is shutdown.
// ---------------------------------------------------------------

int main(int argc, char** argv) {
  // Define the server address and port.
  // "0.0.0.0:50051" means the server listens on port 50051 on all available network interfaces.
  std::string server_address("0.0.0.0:50051");

  // Create a shared instance of DBManager to manage the SQLite database.
  // The database file is named "chat.db". If it doesn't exist, it will be created.
  auto db_manager = std::make_shared<DBManager>("chat.db");

  // Initialize the database:
  // This opens the connection and creates tables if needed.
  if (!db_manager->initialize()) {
    std::cerr << "Failed to initialize the database." << std::endl;
    return 1;
  }

  // Create an instance of ChatServer, passing the database manager.
  // This server instance will handle incoming gRPC requests.
  ChatServer service(db_manager);

  // Set up the gRPC server builder.
  // The builder is used to configure server settings, like the listening port and credentials.
  grpc::ServerBuilder builder;

  // Instruct the builder to listen on the specified address with insecure credentials.
  // InsecureServerCredentials() means no encryption or authentication is used.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  // Register the ChatServer service with the gRPC server.
  builder.RegisterService(&service);

  // Build and start the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Block and wait for the server to shutdown (for example, if the user stops the server).
  server->Wait();
  return 0;
}
