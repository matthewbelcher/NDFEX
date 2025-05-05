#include "../market_data/md_protocol.H"
#include "../order_entry/oe_protocol.H"

#include <vector>
#include <cstdint>
#include <string>
#include <iostream>

namespace ndfex {

class MockSubscriber {
public:
    // Event tracking
    int new_order_count = 0;
    int delete_order_count = 0;
    int modify_order_count = 0;
    int trade_count = 0;
    int fill_count = 0;
    
    // Event details storage
    struct NewOrderEvent {
        uint64_t order_id;
        uint32_t symbol;
        md::SIDE side;
        uint32_t quantity;
        uint32_t display_quantity;
        int32_t price;
        uint8_t flags;
    };
    
    struct DeleteOrderEvent {
        uint64_t order_id;
        bool publish;
    };
    
    struct ModifyOrderEvent {
        uint64_t order_id;
        uint32_t symbol;
        md::SIDE side;
        uint32_t quantity;
        int32_t price;
    };
    
    struct TradeEvent {
        uint64_t order_id;
        uint32_t quantity;
        int32_t price;
    };
    
    struct FillEvent {
        uint64_t order_id;
        uint32_t symbol;
        md::SIDE side;
        uint32_t quantity;
        int32_t price;
        uint8_t flags;
    };
    
    std::vector<NewOrderEvent> new_orders;
    std::vector<DeleteOrderEvent> deleted_orders;
    std::vector<ModifyOrderEvent> modified_orders;
    std::vector<TradeEvent> trades;
    std::vector<FillEvent> fills;
    
    // Reset all counters and vectors
    void reset() {
        new_order_count = 0;
        delete_order_count = 0;
        modify_order_count = 0;
        trade_count = 0;
        fill_count = 0;
        
        new_orders.clear();
        deleted_orders.clear();
        modified_orders.clear();
        trades.clear();
        fills.clear();
    }
    
    // Subscriber interface implementation
    void onNewOrder(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, uint32_t display_quantity, int32_t price, uint8_t flags) {
        new_order_count++;
        new_orders.push_back({order_id, symbol, side, quantity, display_quantity, price, flags});
    }
    
    void onDeleteOrder(uint64_t order_id, bool publish = true) {
        delete_order_count++;
        deleted_orders.push_back({order_id, publish});
    }
    
    void onModifyOrder(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price) {
        modify_order_count++;
        modified_orders.push_back({order_id, symbol, side, quantity, price});
    }
    
    void onTrade(uint64_t order_id, uint32_t quantity, int32_t price) {
        trade_count++;
        trades.push_back({order_id, quantity, price});
    }
    
    void onFill(uint64_t order_id, uint32_t symbol, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags) {
        fill_count++;
        //std::cout << "Fill id: " << order_id << std::endl;
        fills.push_back({order_id, symbol, side, quantity, price, flags});
    }
    
    void onCancelReject(uint64_t order_id) {
        // Track cancel rejections if needed
    }
    
    void finishedPacket(uint32_t symbol) {
        // Called when a packet is completed
    }
    
    // Helper to calculate total filled quantity
    uint32_t calculateTotalFilled(uint64_t order_id) const {
        uint32_t total = 0;
        for (const auto& fill : fills) {
            if (fill.order_id == order_id) {
                //std::cout << "Fill id here " << fill.order_id << " Fill qty "<< fill.quantity << std::endl;
                total += fill.quantity;
            }
        }
        return total;
    }
};

} // namespace ndfex