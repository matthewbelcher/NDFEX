#ifndef ETF_ENGINE_HPP
#define ETF_ENGINE_HPP

#include <string>
#include <iostream>
#include <map>
#include <thread>
#include "etf_bookkeeping.hpp"

class HTTPServer;

class ETFEngine {
    public:
        ETFEngine(const std::string& data_file, int port);
        ~ETFEngine();
        AllClients clients;
        void start_rest_listener();                   // Starts the REST server
        void shutdown();             // Clean shutdown
    private:
        void setup_routes();
        void load_clients_from_file(const std::string& filename);
        HTTPServer * http_server_;
}; 
#endif
