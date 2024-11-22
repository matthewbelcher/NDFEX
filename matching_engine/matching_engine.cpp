#include "matching_engine.H"


int main(int argc, char** argv) {
    // the matching engine has 3 parts:

        // 1. a tcp server that listens for incoming orders
        // 2. for each symbol, a matching engine that matches orders
        // 3. a multicast publisher that publishes market data updates in response to matching engine events

        // the multicast publisher is a subscriber to the matchign engine events via an SPSCQueue
    return 0;
}