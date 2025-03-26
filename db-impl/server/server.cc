#include "server.h"
#include <iostream>

// ---------------------------------------------------------------
// server.cc
//
// This file implements the methods declared in server.h.
// It provides the logic for handling gRPC calls from clients.
// In this example, we implement a simple "SendMessage" method.
// ---------------------------------------------------------------

// Constructor: Initializes the ChatServer with a DBManager for database access.
ChatServer::ChatServer(std::shared_ptr<DBManager> db_manager)
    : db_manager_(db_manager) {
  // The db_manager_ will be used to store chat messages in the database.
}

// SendMessage():
// This method is called when a client sends a chat message.
// It does the following:
// 1. Logs the received message to the console.
// 2. Constructs an SQL INSERT statement to store the message.
// 3. Uses the DBManager to execute the SQL command.
// 4. Prepares a reply to the client, confirming the operation.
grpc::Status ChatServer::SendMessage(grpc::ServerContext* context,
                                       const chat::MessageRequest* request,
                                       chat::MessageReply* reply) {
  // Log the incoming message for debugging purposes.
  std::cout << "Received message: " << request->content() << std::endl;

  // Create an SQL INSERT statement to add the message to the database.
  // Note: In a real-world scenario, make sure to sanitize inputs to prevent SQL injection.
  std::string sql = "INSERT INTO messages (content) VALUES ('" + request->content() + "');";

  // Execute the SQL command using the database manager.
  if (!db_manager_->executeNonQuery(sql)) {
    // If there is an error, return an INTERNAL error status to the client.
    return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to save message to database.");
  }

  // Set a confirmation message in the reply.
  reply->set_confirmation("Message received and stored.");
  return grpc::Status::OK;  // Indicate that the RPC was successful.
}
