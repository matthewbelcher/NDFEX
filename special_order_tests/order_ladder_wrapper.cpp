#include "../matching_engine/order_ladder.H"
#include "mock_subscriber.cpp"

namespace ndfex {

class TestOrderLadder {
public:
    TestOrderLadder(uint32_t symbol) : 
        subscriber(), 
        ladder(&subscriber, symbol),
        symbol(symbol) {}
    
    void addOrder(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags = 0, uint32_t display_quantity = 0) {
        ladder.new_order(order_id, side, quantity, price, flags, 0, display_quantity);
    }
    
    void modifyOrder(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price) {
        ladder.modify_order(order_id, side, quantity, price);
    }
    
    void deleteOrder(uint64_t order_id) {
        ladder.delete_order(order_id);
    }
    
    bool orderExists(uint64_t order_id) {
        // This will require adding a public method to OrderLadder to check
        // if an order exists. For testing purposes, we can check if the order
        // was matched or deleted via the subscriber events.
        
        // Check if the order was deleted
        for (const auto& del : subscriber.deleted_orders) {
            if (del.order_id == order_id) {
                return false;
            }
        }
        
        // For now, assume it exists if not deleted
        return true;
    }
    
    void reset() {
        subscriber.reset();
        // Would need a way to reset the ladder, but for tests we can create a new instance
    }
    
    MockSubscriber subscriber;
    OrderLadder<MockSubscriber> ladder;
    uint32_t symbol;
};

} // namespace ndfex