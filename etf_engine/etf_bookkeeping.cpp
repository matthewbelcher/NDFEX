#include "etf_bookkeeping.hpp"
/*
struct ClientBook {
    std::string password = "";
    std::string username = "";
    //symbols to total holdings
    std::unordered_map<u_int32_t, u_int32_t> holdings;
};
*/
AllClients::AllClients() : total_undy_created(0), total_undy_redeamed(0), current_undy_out(0) {
    Clients.reserve(100); 
}
bool AllClients::authenticate(const std::string& username, const std::string& password) const {
    auto it = Clients.find(username);
    if (it == Clients.end() || it->second.password != password)
        return false;
    return true;
}
bool AllClients::add_client(const std::string& username, const std::string& password) {
    if (client_exists(username))
        return false; //client already exists
    ClientBook newClient;
    newClient.username = username;
    newClient.password = password;
    //we only care about the etf, no need to store other symbols 
    newClient.holdings = {{3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}}; //KNAN STED RYAN SHAG DBRT UNDY
    Clients[username] = newClient;
    return true;
}

bool AllClients::client_exists(const std::string& username) const {
    auto it = Clients.find(username);
    if (it == Clients.end())
        return false;
    return true;
}
ClientBook* AllClients::get_client(const std::string& username) {
    auto it = Clients.find(username);
    if (it == Clients.end()) return nullptr;
    return &(it->second);
}
//functions assume user has successfully been authenticated, so its the etf_engines job to call authenticate first
bool AllClients::credit_etf(const std::string& username, uint32_t amount) {
     //add to user account
    ClientBook* client = get_client(username);
    if (!client) return false;
     //etf is symbol 8
    client->holdings[8] += amount;
    return true;
}
bool AllClients::debit_etf(const std::string& username, uint32_t amount) {
    ClientBook* client = get_client(username);
    if(client->holdings[8] >= amount) {
            client->holdings[8] -= amount;
            return true;
    }
    return false;
}
uint32_t AllClients::get_etf_balance(const std::string& username) const {
    return Clients.at(username).holdings.at(8);
}
// Stock handling
bool AllClients::debit_stock(const std::string& username, uint32_t symbol_id, uint32_t amount) {
    //check if stock is a valid symbol
    ClientBook* client = get_client(username);
    if (!client) return false;
    auto it = client->holdings.find(symbol_id);
    if(it != client->holdings.end() && client->holdings[symbol_id] >= amount) {
            client->holdings[symbol_id] -= amount;
            return true;
    }
    return false;
}
bool AllClients::credit_stock(const std::string& username, uint32_t symbol_id, uint32_t amount) {
    //check if stock is a valid symbol
    ClientBook* client = get_client(username);
    if (!client) return false;
    auto it = client->holdings.find(symbol_id);
    if(it != client->holdings.end()) {
            client->holdings[symbol_id] += amount;
            return true;
    }
    return false;
}
uint32_t AllClients::get_stock_balance(const std::string& username, uint32_t symbol_id) {
    //check if stock is a valid symbol
    ClientBook* client = get_client(username);
    if (!client) return false;
    auto it = client->holdings.find(symbol_id);
    if(it != client->holdings.end()) {
        return client->holdings[symbol_id];
    }
    return 0;
}
std::unordered_map<uint32_t, uint32_t> AllClients::get_holdings(const std::string &username) {
    auto it = Clients.find(username);
    if (it != Clients.end()) {
        return it->second.holdings;
    } else {
        return {}; // Empty map if user doesn't exist
    }
}

// Counters
void AllClients::record_creation(uint32_t amount) {
    total_undy_created += amount;
    current_undy_out += amount;
}
void AllClients::record_redemption(uint32_t amount) {
    total_undy_redeamed += amount;
    current_undy_out -= amount;
}
bool AllClients::create_etf(const std::string &username, uint32_t amount) {
    //check to make sure we have all the underlyings
    ClientBook* client = get_client(username);
    if (!client) return false;
    for (const auto& [symbol, this_amount] : client->holdings) {
        if (symbol == 8) continue;
        if (this_amount < amount) return false;
    }
    //remove holdings, add etf
    for (const auto& [symbol, _] : client->holdings) {
        if (symbol == 8) continue;
        debit_stock(username, symbol, amount);
    }
    credit_etf(username, amount);
    record_creation(amount);
    return true;
}
bool AllClients::redeem_etf(const std::string &username, uint32_t amount){
    ClientBook* client = get_client(username);
    if (!client) return false;
    if (client->holdings[8] < amount)
        return false;
    for (const auto& [symbol, _] : client->holdings) {
        if (symbol == 8) continue;
        credit_stock(username, symbol, amount);
    }
    debit_etf(username, amount);
    record_redemption(amount);
    return true;
}

