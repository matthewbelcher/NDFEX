#include "etf_engine.hpp"
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

using namespace Pistache;
using json = nlohmann::json;

ETFEngine::ETFEngine(const std::string& data_file, int port) {
    clients = AllClients();
    load_clients_from_file(data_file);

    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(port));
    httpEndpoint = std::make_shared<Pistache::Http::Endpoint>(addr);

    Pistache::Http::Endpoint::Options opts;
    opts.threads(1); // One-threaded REST handler
    httpEndpoint->init(opts);

    setup_routes();
}

void ETFEngine::load_clients_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) return;

    json j;
    file >> j;
    for (auto& el : j.items()) {
        const auto& user = el.key();
        const auto& data = el.value();
        clients.add_client(user, data["password"]);
        for (auto& [symbol, amount] : data["holdings"].items()) {
            clients.credit_stock(user, std::stoi(symbol), amount);
        }
    }
}

void ETFEngine::setup_routes() {
    using namespace Pistache::Rest;

    Routes::Post(router, "/create", Routes::bind(&ETFEngine::handle_create, this));
    Routes::Post(router, "/redeem", Routes::bind(&ETFEngine::handle_redeem, this))

    httpEndpoint->setHandler(router.handler());
}
void ETFEngine::run() {
    httpEndpoint->serve();
}
void ETFEngine::shutdown() {
    httpEndpoint->shutdown();
}
void ETFEngine::handle_create(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto body = json::parse(request.body());
    std::string username = body["username"];
    std::string password = body["password"];
    uint32_t amount = body["amount"];
    if (clients.authenticate(username, password))
        if(clients.create_etf(username, password, amount)) {
            response.send(Pistache::Http::Code::Ok, "Created");
        }
        else {
        response.send(Pistache::Http::Code::Unauthorized, "Insufficient Holdings");
        }
    else
        response.send(Pistache::Http::Code::Unauthorized, "Invalid credentials");
}
void ETFEngine::handle_redeem(const Pistache::Rest::Request& request, Pistache::Http::ResponseWriter response) {
    auto body = json::parse(request.body());
    std::string username = body["username"];
    std::string password = body["password"];
    uint32_t amount = body["amount"];
    if (clients.authenticate(username, password]))
        if(clients.redeem_etf(username, password, amount])) {
            response.send(Pistache::Http::Code::Ok, "Redeemed");
        }
        else {
        response.send(Pistache::Http::Code::Unauthorized, "Insufficient Holdings");
        }
    else
        response.send(Pistache::Http::Code::Unauthorized, "Invalid credentials");
}
int main(int argc, char** argv) {
    ETFEngine engine("users.txt");
    engine.load_state_from_file("users.txt");


    std::thread server_thread([&engine]() {
        engine.start_rest_listener(8080);
    });
    //read users.txt to get Clients and re add them to the book

    //add clients holdings to the book

    //begin listening
    // Meanwhile, the main thread could handle other logic:
    while (true) {
        // Poll for multicast updates or process queues
        // sleep or yield if needed
    }

    server_thread.join();
    return 0;
}