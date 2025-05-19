

class OpenOrderTracker {

};

class PositionTracker {

    void onFill();
};

class OrderEntry {

    void send_order() {
        // check position

        // check order size

        //        ....



    }

    void process() {

        // read from the socket

        // parse the responses

        // call back to the trading strategy

        strategy.onFill(Order*, prc, qty);
        strategy.onAck(Order*);
    }
}

class TradingStrategy {

    TradingStrategy(OrderEntry oe) {
        oe.registerListener(this);
    }

    void process() {

        // look at the quotes from the order book
        ob.get_best_bid();

        // check positions / pnl / open orders

        // send new orders


    }

    void onFill(Order* , prc, qty) {
        pos += qty;
        pnl += prc*qty;

    }


};

int main(int argc, char* argv[]) {

    // add a signal handler
    std::vector<OrderBook> obs;
    for (int i = 0; i < num_symbols; ++i) {
        obs.push_back(new OrderBook);

    }

    MarketDataListener listener(obs);

    OrderEntry order_entry;

    TradingStrategy strategy(obs, order_entry);

    std::atomic<bool> stopped;
    while (!stopped) {

        listener.process(); // read from the multicast market data

        order_entry.process(); // check for responses from exchange

        strategy.process(); // let strategy do its thing


    }





}

std::atomic<int64_t> 