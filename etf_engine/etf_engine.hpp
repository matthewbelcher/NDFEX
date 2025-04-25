#pragma once
#include "etf_bookkeeping.hpp"
#include <thread>
#include <string>
#include <mutex>
#include <atomic>
#include <iostream>
#include <string>
#include "../external/concurrentqueue/concurrentqueue.h"
#include <pistache/http.h>
#include <pistache/router.h>



class ETFEngine {
    public:
        ETFEngine(const std::string& data_file, int port = 9080);
        void run();                   // Starts the REST server
        void shutdown();             // Clean shutdown
    private:
        void setup_routes();
        void load_clients_from_file(const std::string& filename);

        //route handlers
        void handle_create(const Pistache::Rest::Request &request, Pistache::Http::ResponseWriter response);
        void handle_redeem(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response);
        AllClients clients;
        std::shared_ptr<Pistache::Http::Endpoint> httpEndpoint;
        Pistache::Rest::Router router;



}; 
