#include "md_mcast.H"
#include "order_ladder.H"
#include "spsc_md_queue.H"
#include "spsc_subscriber.H"

#include <thread>
#include <vector>
#include <memory>

int main(int argc, char* argv[]) {

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <mcast_addr> <mcast_port> <bind_ip>" << std::endl;
        return 1;
    }
    std::vector<std::unique_ptr<ndfex::SPSCMDQueue>> queues;
    queues.emplace_back(std::make_unique<ndfex::SPSCMDQueue>(1000));

    std::string mcast_addr{argv[1]};
    uint16_t mcast_port = std::stoi(argv[2]);
    std::string bind_ip{argv[3]};

    ndfex::MarketDataPublisher publisher(queues, mcast_addr, mcast_port, bind_ip);
    ndfex::SPSC_Subscriber subscriber(*queues[0]);
    ndfex::OrderLadder<ndfex::SPSC_Subscriber> ladder(&subscriber, 1234);

    std::thread t([&publisher] {
        while (true) publisher.process();
    });

    // simulate some market data events
    for (int i = 0; i < 1000000; ++i) {
        ladder.new_order(i, ndfex::md::SIDE::BUY, 10, 50, 0);
    }

    for (int i = 0; i < 500000; ++i) {
        ladder.modify_order(i, ndfex::md::SIDE::SELL, 5, 50);
    }

    for (int i = 0; i < 1000000; ++i) {
        if (rand() % 2 == 0) ladder.delete_order(i);
    }

    t.join();

    return 0;
}