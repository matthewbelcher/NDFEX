#include "etf_engine.hpp"

int main(int argc, char**argv) {
    int port = 9080;
    if (argc > 1) {
            port = std::stoi(argv[1]);
    }
    ETFEngine engine;
    egine.run_rest_server(port);
    return 0;
}