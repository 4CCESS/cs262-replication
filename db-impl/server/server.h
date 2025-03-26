#ifndef SERVER_H
#define SERVER_H

// ---------------------------------------------------------------
// server.h
//
// This header defines the ChatServer class, which implements a
// gRPC service for the chat application.
//
// gRPC (Google Remote Procedure Call) is a high-performance,
// open-source RPC framework that uses protocol buffers (protobuf)
// to define the service and messages.
// ---------------------------------------------------------------

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "chat.grpc.pb.h"  // gRPC-generated header from chat.proto
#include "db_manager.h"    // Header for the database manager

// ChatServer:
// This class implements the chat service defined in your proto file.
// It inherits from the generated chat::ChatService::Service class.
class ChatServer final : public chat::ChatService::Service {
 public:
  // Constructor:
  // Accepts a shared pointer to a DBManager, which handles database operations.
  ChatServer(std::shared_ptr<DBManager> db_manager);

  // SendMessage:
  // This method implements the gRPC service call for sending a message.
  // Clients call this method to send a chat message to the server.
  // Parameters:
  //  - context: Contains information about the RPC (e.g., metadata, deadlines).
  //  - request: Contains the client's message (defined in chat.proto).
  //  - reply: Used to send back a response to the client.
  // Returns:
  //  - A grpc::Status indicating whether the call was successful.
  grpc::Status SendMessage(grpc::ServerContext* context,
                             const chat::MessageRequest* request,
                             chat::MessageReply* reply) override;

  // Additional gRPC methods (e.g., for retrieving messages) could be added here.

 private:
  // A shared pointer to the DBManager instance.
  // This allows the server to store and retrieve messages from the SQLite database.
  std::shared_ptr<DBManager> db_manager_;
};

#endif // SERVER_H
