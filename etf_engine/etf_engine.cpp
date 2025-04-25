#include "etf_engine.hpp"
#include "http_server.hpp"
#include <sstream>
#include <fstream>

ETFEngine::ETFEngine(const std::string& data_file, int port) :  http_server_(new HTTPServer(port, *this)) {
    clients = AllClients();
    load_clients_from_file(data_file);
}
ETFEngine::~ETFEngine() {
    // cleanup
}
void ETFEngine::load_clients_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Failed to open client data file: " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string username, password;
        iss >> username >> password;

        if (username.empty() || password.empty()) continue;
        
        clients.add_client(username, password);
        //std::cout << "client" << username << ".\npass " << password << "." << std::endl;
        int symbol;
        int amount;
        while (iss >> symbol >> amount) {
            clients.credit_stock(username, symbol, amount);
        }
    }
}

void ETFEngine::start_rest_listener() {
    http_server_->start();
    /*std::thread server_thread([this] {
        std::cerr << " or made\n" << std::endl;
        http_server_->start();  // Start the server
    });
    std::cerr << " engine made\n" << std::endl;
    server_thread.detach(); */ // Detach the thread to run in the background
}

void ETFEngine::shutdown() {
    
}
