#include "test_framework.H"
#include "../etf_bookkeeping.hpp"

using namespace test;


void test_etf_bookkeeping_add_client() {
//bool add_client(const std::string& username, const std::string& password); 
    AllClients book;
    assert_true(book.add_client("alice", "password"), "Client should be added");
    assert_false(book.add_client("alice", "pass"), "Duplicate clients are not allowed");
}
void test_etf_bookkeeping__client_exists() {
    //bool client_exists(const std::string& username) const;
    AllClients book;
    book.add_client("alice", "password");
    assert_true(book.client_exists("alice"), "Client should be added");
    assert_false(book.client_exists("bob"), "Client should not exist");
}
void test_etf_bookkeeping_athenticate() {
    //authenticate(const std::string& username, const std::string& password) 
    AllClients book;
    book.add_client("alice", "password");
    assert_true(book.authenticate("alice", "password"), "Client should be authenticated");
    assert_false(book.authenticate("alice", "wrong"), "Client should not be logged in, wrong password");
    assert_false(book.authenticate("bob", "passwd"), "Client never added");
}
void test_etf_bookkeeping_credit_etf() {
    //bool credit_etf(const std::string& username, uint32_t amount); //add to user account
    AllClients book;
    book.add_client("alice", "password");
    assert_true(book.credit_etf("alice", 20), "ETF should increase"); 
}
void test_etf_bookkeeping_debit_etf() {
    //bool debit_etf(const std::string& username, uint32_t amount);
    AllClients book;
    book.add_client("alice", "password");
    book.credit_etf("alice", 20);
    assert_true(book.debit_etf("alice", 10), "Debit etf should work");
    assert_false(book.debit_etf("alice", 15), "Debit should fail, insufficient funds"); 
}
void test_etf_bookkeeping_get_etf_balance() {
    //uint32_t get_etf_balance(const std::string& username) const;
    AllClients book;
    book.add_client("alice", "password");
    book.credit_etf("alice", 20);
    assert_equal(book.get_etf_balance("alice"), 20, "Balance should be 20");
    book.debit_etf("alice", 10);
    assert_equal(book.get_etf_balance("alice"), 10, "Balance should be 10");
    book.debit_etf("alice", 15); 
    assert_equal(book.get_etf_balance("alice"), 10, "Balance should be 10");
}
void test_etf_bookkeeping_credit_stock() {
//bool credit_stock(const std::string& username, uint32_t symbol_id, uint32_t amount);
    AllClients book;
    book.add_client("alice", "password");
    assert_true(book.credit_stock("alice", 3, 5), "Credit stock should work");
    assert_false(book.credit_stock("alice", 10, 5), "Should be false, Symbol id DNE");
}
void test_etf_bookkeeping_debit_stock() {
//bool debit_stock(const std::string& username, uint32_t symbol_id, uint32_t amount);
    AllClients book;
    book.add_client("alice", "password");
    book.credit_stock("alice", 3, 5);
    assert_true(book.debit_stock("alice", 3, 4), "Client should be able to debit stock");
    assert_false(book.debit_stock("alice", 3, 5), "Should be false, insufficient funds");

}
void test_etf_bookkeeping_get_stock_balance() {
//uint32_t get_stock_balance(const std::string& username, uint32_t symbol_id);
    AllClients book;
    book.add_client("alice", "password");
    book.credit_stock("alice", 3, 5);
    assert_equal(book.get_stock_balance("alice", 3), 5, "Stock balance should match");
    book.debit_stock("alice", 3, 4);
    assert_equal(book.get_stock_balance("alice", 3), 1, "Stock balance should match");
    book.debit_stock("alice", 3, 5);
    assert_equal(book.get_stock_balance("alice", 3), 1, "Stock balance should match");
}
void test_etf_bookkeeping_record_creation() {
//void record_creation(uint32_t amount);
    AllClients book;
    book.record_creation(10);
    assert_equal(book.total_undy_created, 10, "Creation count should be 10");
    book.record_creation(5);
    assert_equal(book.total_undy_created, 15, "Creation count should be 15");

}
void test_etf_bookkeeping_record_redemption() {
//void record_redemption(uint32_t amount);
    AllClients book;
    book.record_redemption(10);
    assert_equal(book.total_undy_redeamed, 10, "Redemption count should be 10");
    book.record_redemption(5);
    assert_equal(book.total_undy_redeamed, 15, "Redemption count should be 15");
}
int main() {
    TestSuite etf_suite("ETF_Bookkeeping Tests");

    etf_suite.add_test("ETF Authenication", test_etf_bookkeeping_athenticate);
    etf_suite.add_test("Add Clients", test_etf_bookkeeping_add_client);
    etf_suite.add_test("Client Existence", test_etf_bookkeeping__client_exists);
    etf_suite.add_test("Credit Client Etf", test_etf_bookkeeping_credit_etf);
    etf_suite.add_test("Debit Client Etf", test_etf_bookkeeping_debit_etf);
    etf_suite.add_test("Get Etf Balance", test_etf_bookkeeping_get_etf_balance);
    etf_suite.add_test("Credit Stock", test_etf_bookkeeping_credit_stock);
    etf_suite.add_test("Debit Stock", test_etf_bookkeeping_debit_stock);
    etf_suite.add_test("Get Stock Balance", test_etf_bookkeeping_get_stock_balance);
    etf_suite.add_test("Record Creation", test_etf_bookkeeping_record_creation);
    etf_suite.add_test("Record Redemption", test_etf_bookkeeping_record_redemption);

    auto results = etf_suite.run_all();
    int passed = 0;
    for (const auto& result : results) {
        if (result.passed) passed++;
    }
    std::cout << "Results: " << passed << " passed, " << (results.size() - passed) << " failed" << std::endl;
    
    return (passed == results.size()) ? 0 : 1;
}