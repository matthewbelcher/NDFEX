#pragma once
#include <unordered_map>
#include <string>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>
/*For reference
//define UNDY & components
{3, 5, 1, 1000, 10000000, 0}, //KNAN
{4, 5, 1, 1000, 10000000, 0}, //STED
{5, 5, 1, 1000, 10000000, 0}, //RYAN
{6, 5, 1, 1000, 10000000, 0}, //SHAG
{7, 5, 1, 1000, 10000000, 0}, //DBRT
{8, 10, 1, 1000, 10000000, 0}, //UNDY
*/

struct ClientBook {
    std::string password = "";
    std::string username = "";
    //symbols to total holdings
    std::unordered_map<u_int32_t, u_int32_t> holdings;
};

class AllClients {
    private:
        
    public:
        AllClients();
        //client username to ClientBook
        std::unordered_map<std::string, ClientBook> Clients;
        uint32_t total_undy_created = 0;
        uint32_t total_undy_redeamed = 0;
        uint32_t current_undy_out = 0;
        bool authenticate(const std::string& username, const std::string& password) const;
        bool add_client(const std::string& username, const std::string& password); 
        bool client_exists(const std::string& username) const;
        ClientBook* get_client(const std::string& username);
        bool credit_etf(const std::string& username, uint32_t amount); //add to user account
        bool debit_etf(const std::string& username, uint32_t amount);
        uint32_t get_etf_balance(const std::string& username) const;

        // Stock handling
        bool credit_stock(const std::string& username, uint32_t symbol_id, uint32_t amount);
        bool debit_stock(const std::string& username, uint32_t symbol_id, uint32_t amount);
        uint32_t get_stock_balance(const std::string& username, uint32_t symbol_id);
        //creation/deletion
        bool create_etf(const std::string &username, uint32_t amount);
        std::unordered_map<u_int32_t, u_int32_t> get_holdings(const std::string &username);
        bool redeem_etf(const std::string &username, uint32_t amount);
        // Counters
        void record_creation(uint32_t amount);
        void record_redemption(uint32_t amount);
        
};

