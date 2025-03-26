#include "client.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

void printHelp() {
    std::cout << "Available commands:" << std::endl;
    std::cout << "  /register <username> <password> - Register a new user" << std::endl;
    std::cout << "  /login <username> <password> - Login with existing user" << std::endl;
    std::cout << "  /quit - Exit the application" << std::endl;
    std::cout << "  <message> - Send a message (must be logged in)" << std::endl;
}

int main(int argc, char** argv) {
    std::string server_address = "localhost:50051";
    
    // Parse command line arguments if provided
    if (argc > 1) {
        server_address = argv[1];
    }
    
    std::cout << "Connecting to server at " << server_address << std::endl;
    ChatClient client(server_address);
    
    // Set up message callback
    client.SetMessageCallback([](const std::string& sender, const std::string& content) {
        std::cout << sender << ": " << content << std::endl;
    });
    
    std::cout << "Simple Chat Client" << std::endl;
    printHelp();
    
    std::string line;
    bool running = true;
    
    while (running) {
        std::cout << "> ";
        std::getline(std::cin, line);
        
        if (line.empty()) {
            continue;
        }
        
        if (line == "/quit") {
            running = false;
        } else if (line == "/help") {
            printHelp();
        } else if (line.substr(0, 9) == "/register") {
            // Parse username and password
            size_t pos1 = line.find(' ', 10);
            if (pos1 == std::string::npos) {
                std::cout << "Usage: /register <username> <password>" << std::endl;
                continue;
            }
            
            std::string username = line.substr(10, pos1 - 10);
            std::string password = line.substr(pos1 + 1);
            
            if (client.Register(username, password)) {
                std::cout << "Registration successful! You are now logged in as " << username << std::endl;
                client.StartMessageListener();
            } else {
                std::cout << "Registration failed." << std::endl;
            }
        } else if (line.substr(0, 6) == "/login") {
            // Parse username and password
            size_t pos1 = line.find(' ', 7);
            if (pos1 == std::string::npos) {
                std::cout << "Usage: /login <username> <password>" << std::endl;
                continue;
            }
            
            std::string username = line.substr(7, pos1 - 7);
            std::string password = line.substr(pos1 + 1);
            
            if (client.Login(username, password)) {
                std::cout << "Login successful! You are now logged in as " << username << std::endl;
                client.StartMessageListener();
            } else {
                std::cout << "Login failed." << std::endl;
            }
        } else {
            // Send message
            if (client.IsLoggedIn()) {
                client.SendMessage(line);
            } else {
                std::cout << "You must be logged in to send messages. Use /login or /register." << std::endl;
            }
        }
    }
    
    return 0;
}
