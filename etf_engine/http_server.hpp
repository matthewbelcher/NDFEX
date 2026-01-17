#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include <string>
#include <unordered_map>
#include <memory>
#include "etf_engine.hpp"

// Simple HTTPServer class for handling requests
class HTTPServer {
public:
    // Constructor - Takes an ETFEngine reference (to interact with the core logic)
    HTTPServer(int port, ETFEngine& engine);
    // Starts the HTTP server to listen on the specified port
    void start();

private:
    ETFEngine& engine_;  // Reference to the ETFEngine
    int port_;           // The port on which the server listens

    // Helper method to send an HTTP response
    void send_response(int client_socket, const std::string& status, const std::string& body);

    // Request handlers
    void handle_request(int client_socket);
    void handle_create_etf_request(int client_socket, const std::string& request);
    void handle_redeem_etf_request(int client_socket, const std::string& request);
    void handle_holdings_request(int client_socket, const std::string& path);
    void handle_register_request(int client_socket, const std::string& request);
    
    // Parse a request to extract data such as username, password, etc.
    //std::unordered_map<std::string, std::string> parse_request(const std::string& request);
    std::unordered_map<std::string, std::string> parse_url_encoded_body(const std::string& body);

}; 
#endif