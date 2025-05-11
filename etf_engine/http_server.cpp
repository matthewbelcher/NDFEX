#include "http_server.hpp"
#include <iostream>
#include <cstring>
#include <thread>
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>

HTTPServer::HTTPServer(int port, ETFEngine& engine) : engine_(engine), port_(port) {}

void HTTPServer::start() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Failed to create socket\n";
        return;
    }
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        std::cerr << "Failed to bind to port\n";
        return;
    }
    
    if (listen(server_fd, 3) == -1) {
        std::cerr << "Failed to listen on port\n";
        return;
    }
    
    std::cout << "Server is running on port " << port_ << "\n";
    while (true) {
        int client_socket = accept(server_fd, nullptr, nullptr);
        if (client_socket == -1) {
            std::cerr << "Failed to accept connection\n";
            continue;
        }

        std::thread(&HTTPServer::handle_request, this, client_socket).detach();
    }
}
/*
void HTMLServer::handle_client(int client_fd) {
    char buffer[4096];
    int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes] = '\0';
    std::string request(buffer);

    // Basic parse of POST body
    size_t body_start = request.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        close(client_fd);
        return;
    }

    std::string body = request.substr(body_start + 4);
    auto params = parse_url_encoded_body(body);

    std::string response;
    if (params["username"].empty() || params["password"].empty()) {
        response = "HTTP/1.1 400 Bad Request\r\n\r\nMissing username or password";
    } else if (params["action"] == "redeem") {
        bool success = engine_.clients.add_client(params["username"], params["password"]); 
        response = success ? "HTTP/1.1 200 OK\r\n\r\nAdd Client successfully" : "HTTP/1.1 400 Bad Request\r\n\r\nAdd Client failed";
    } else {
        if (!engine_.clients.authenticate(params["username"], params["password"])) {
            response = "HTTP/1.1 401 Unauthorized\r\n\r\nAuthentication failed";
        } else if (params["action"] == "redeem") {
            bool success = engine_.clients.redeem_etf(params["username"], std::stoi(params["amount"]));
            response = success ? "HTTP/1.1 200 OK\r\n\r\nRedeemed successfully" : "HTTP/1.1 400 Bad Request\r\n\r\nRedeem failed";
        } else if (params["action"] == "create") {
            bool success = engine_.clients.create_etf(params["username"], std::stoi(params["amount"]));
            response = success ? "HTTP/1.1 200 OK\r\n\r\nCreated successfully" : "HTTP/1.1 400 Bad Request\r\n\r\nCreate failed";
        } else {
            response = "HTTP/1.1 400 Bad Request\r\n\r\nInvalid action";
        }
    }

    send(client_fd, response.c_str(), response.length(), 0);
    close(client_fd);
}*/

std::unordered_map<std::string, std::string> HTTPServer::parse_url_encoded_body(const std::string& body) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream iss(body);
    std::string token;

    while (std::getline(iss, token, '&')) {
        auto pos = token.find('=');
        if (pos != std::string::npos) {
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);
            params[key] = value;
        }
    }
    return params;
}
void HTTPServer::handle_request(int client_socket) {
    char buffer[1024];
    ssize_t read_size = read(client_socket, buffer, sizeof(buffer));
    if (read_size == -1) {
        std::cerr << "Failed to read from socket\n";
        close(client_socket);
        return;
    }
    // Simple HTTP request parsing
    std::string request(buffer, read_size);
    std::string method = request.substr(0, request.find(' '));
    std::string path = request.substr(request.find(' ') + 1, request.find(' ', request.find(' ') + 1) - (request.find(' ') + 1));
    std::cerr << "Routing\n";
    // Routing based on method and path
    if (method == "POST" && path == "/redeem") {
        handle_redeem_etf_request(client_socket, request);
    } else if (method == "POST" && path == "/create") {
        handle_create_etf_request(client_socket, request);
    } else if (method == "POST" && path == "/register") {
        handle_register_request(client_socket, request);
    } else if (method == "GET" && path.find("/holdings") == 0) {
        handle_holdings_request(client_socket, path);
    }
    else {
        send_response(client_socket, "HTTP/1.1 404 Not Found", "Page not found");
    }

    close(client_socket);
}
void HTTPServer::handle_register_request(int client_socket, const std::string& request) {
    size_t body_start = request.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "No body found");
        return;
    }

    std::string body = request.substr(body_start + 4);
    auto params = parse_url_encoded_body(body);

    if (params["username"].empty() || params["password"].empty()) {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Missing username or password");
        return;
    }

    if (engine_.clients.add_client(params["username"], params["password"])) {
        send_response(client_socket, "HTTP/1.1 200 OK", "Client registered");
    } else {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Client already exists or failed");
    }
}
void HTTPServer::handle_redeem_etf_request(int client_socket, const std::string& request) {
    // Parse the request to get parameters (username, password, amount)
    std::unordered_map<std::string, std::string> params = parse_url_encoded_body(request);
    std::string username = params["username"];
    std::string password = params["password"];
    int amount = std::stoi(params["amount"]);  // Convert amount to integer

    if (username.empty() || password.empty() || amount <= 0) {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Missing or invalid parameters");
        return;
    }

    // Authenticate the user
    if (!engine_.clients.authenticate(username, password)) {
        send_response(client_socket, "HTTP/1.1 401 Unauthorized", "Authentication failed");
        return;
    }

    // Redeem the ETF for the user
    bool success = engine_.clients.redeem_etf(username, amount);
    if (success) {
        send_response(client_socket, "HTTP/1.1 200 OK", "ETF redeemed successfully");
    } else {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Failed to redeem ETF or insufficient funds");
    }
}
void HTTPServer::handle_create_etf_request(int client_socket, const std::string& request) {
    // Parse the request to get parameters (username, password, amount)
    std::unordered_map<std::string, std::string> params = parse_url_encoded_body(request);
    std::string username = params["username"];
    std::string password = params["password"];
    int amount = std::stoi(params["amount"]);  // Convert amount to integer

    if (username.empty() || password.empty() || amount <= 0) {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Missing or invalid parameters");
        return;
    }

    // Authenticate the user
    if (!engine_.clients.authenticate(username, password)) {
        send_response(client_socket, "HTTP/1.1 401 Unauthorized", "Authentication failed");
        return;
    }

    // Create the ETF for the user
    bool success = engine_.clients.create_etf(username, amount);
    if (success) {
        send_response(client_socket, "HTTP/1.1 200 OK", "ETF created successfully");
    } else {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Failed to create ETF");
    }
}
void HTTPServer::handle_holdings_request(int client_socket, const std::string& path) {
    size_t query_start = path.find("?");
    if (query_start == std::string::npos) {
        send_response(client_socket, "HTTP/1.1 400 Bad Request", "Missing query");
        return;
    }

    std::unordered_map<std::string, std::string> params = parse_url_encoded_body(path.substr(query_start + 1));

    std::string username = params["username"];
    std::string password = params["password"];
    //std::cout << "User " << username << "\nPass " << password << std::endl;
    if (!engine_.clients.authenticate(username, password)) {
        send_response(client_socket, "HTTP/1.1 401 Unauthorized", "Authentication failed");
        return;
    }

    auto holdings = engine_.clients.get_holdings(username);
    std::stringstream json;
    json << "{";
    for (auto it = holdings.begin(); it != holdings.end(); ++it) {
        if (it != holdings.begin()) json << ",";
        json << "\"" << it->first << "\":" << it->second;
    }
    json << "}";

    send_response(client_socket, "HTTP/1.1 200 OK", json.str());
}

void HTTPServer::send_response(int client_socket, const std::string& status_line, const std::string& body) {
    std::stringstream response;
    response << status_line << "\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "\r\n";
    response << body;

    write(client_socket, response.str().c_str(), response.str().size());
}
    
    