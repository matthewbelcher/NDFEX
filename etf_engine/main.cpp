#include "etf_engine.hpp"
#include "http_server.hpp"
#include <iostream>
#include <cstdlib> // For std::stoi


int main(int argc, char**argv) {
    // Create ETFEngine which will start the HTTP server
    int port  = 8080;
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <data_file> <port_number>" << std::endl;        
        return 1; // Exit with error
    }
    try {
        port = std::stoi(argv[2]); // Convert string to integer
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid port number: " << argv[2] << std::endl;
        return 1;
    } catch (const std::out_of_range& e) {
        std::cerr << "Port number out of range: " << argv[2] << std::endl;
        return 1;
    }
    std::cout << "Starting server on port " << port << "..." << std::endl;
    ETFEngine engine(argv[1], port);  // Server runs on port 8080
    
    // The server will continue running in the background
    engine.start_rest_listener();

    return 0;
}