#pragma once

#include <cstdint>
#include <unordered_map>
#include <map>
#include <stdexcept>
#include <iostream>

#include "../market_data/md_protocol.H"
#include "../order_entry/oe_protocol.H"
#include "exceptions.H"

namespace ndfex {

    template <typename Subscriber>
    class OrderLadder {

    public:

        class Order {
        public:
            uint64_t order_id;
            uint64_t timestamp;
            uint32_t quantity; // current visible quantity **
            uint32_t filled_quantity;
            // Updated for iceberg orders
            uint32_t total_quantity; // total order size
            uint32_t display_quantity; // displayed quantity for iceberg orders
            int32_t price;
            md::SIDE side;
            uint8_t flags;

            // Doubly linked list pointers at the same price level
            Order* next = nullptr;
            Order* prev = nullptr;
        };

        class PriceLevel {
        public:
            Order* head = nullptr;
            Order* tail = nullptr;
        };

    private:

        static const std::size_t INITIAL_ORDER_CAPACITY = 1e7;

        std::unordered_map<uint64_t, Order> orders{INITIAL_ORDER_CAPACITY};
        std::map<int32_t, PriceLevel, std::greater<int32_t>> buy_levels;
        std::map<int32_t, PriceLevel, std::less<int32_t>> sell_levels;

        Subscriber* const subscriber;
        const uint32_t symbol;

    public:
        OrderLadder(Subscriber* subscriber, uint32_t symbol) : subscriber(subscriber), symbol(symbol) {};

        ~OrderLadder() = default;

        template <bool notify = true>
        void new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, uint32_t filled_quantity = 0, uint32_t display_quantity = 0);

        template <bool notify = true>
        void delete_order(uint64_t order_id);

        void modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price);

    private:

        template <typename PriceLevels>
        void match(uint64_t order_id, md::SIDE side, uint32_t& quantity, int32_t price, uint8_t flags,
                   PriceLevels& opposite_side_price_levels);

        template <typename PriceLevels>
        void handle_new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, PriceLevels& price_levels,
                              bool notify, uint32_t filled_quantity, uint32_t display_quantity);

        template <typename PriceLevels>
        void handle_delete_order(uint64_t order_id, PriceLevels& price_levels);
    };

    template <typename Subscriber>
    template <bool notify>
    void OrderLadder<Subscriber>::new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags,
                                            uint32_t filled_quantity, uint32_t display_quantity) {
        if (side == md::SIDE::BUY) {
            handle_new_order(order_id, side, quantity, price, flags, buy_levels, notify, filled_quantity, display_quantity);
        } else {
            handle_new_order(order_id, side, quantity, price, flags, sell_levels, notify, filled_quantity, display_quantity);
        }
    }

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::handle_new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags,
                                                   PriceLevels& price_levels, bool notify, uint32_t filled_quantity, uint32_t display_quantity) {
        //std::cout << "Order received!" << std::endl;
        //std::cout << " Price=" << price << " Quantity=" << quantity << std::endl;
        
        if (quantity > filled_quantity) {
            quantity = quantity - filled_quantity;
        } else {
            throw std::runtime_error("filled quantity > quantity on new order!");
        }
    
        uint32_t prematch_quantity = quantity;
        if (side == md::SIDE::BUY) {
            match(order_id, side, quantity, price, flags, sell_levels);
        } else {
            match(order_id, side, quantity, price, flags, buy_levels);
        }
    
        // IOC orders are not added to the ladder
        if (quantity > 0 && (flags & static_cast<uint8_t>(oe::ORDER_FLAGS::IOC))) {
            if (notify) {
                if (!(flags & static_cast<uint8_t>(oe::ORDER_FLAGS::AON))) {
                subscriber->onDeleteOrder(order_id, false);
                }
                // still need to notify sender that it was closed
                
            }
            return;
        }
    
        if (quantity == 0) {
            // order is fully matched, still notify subscriber but do not add to the ladder
            return;
        }
    
        if (orders.find(order_id) != orders.end()) {
            // order already exists
            throw std::runtime_error("Order already exists");
        }
    
        if (quantity > prematch_quantity) {
            throw std::runtime_error("Impossible");
        }
    
        bool is_iceberg = flags & static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG);
        
        if (is_iceberg) {
            // Create new order
            Order& order = orders[order_id];
            order.order_id = order_id;
            order.timestamp = 0;
            order.quantity = quantity;
            order.display_quantity = std::min(quantity, display_quantity); // Show only display quantity
            order.filled_quantity = prematch_quantity - quantity;
            order.price = price;
            order.side = side;
            order.flags = flags; // Keep original flags
            order.next = nullptr;
            order.prev = nullptr;
    
            auto it = price_levels.find(price);
            if (it == price_levels.end() || it->second.head == nullptr || it->second.tail == nullptr) {
                // Either price level doesn't exist or it's empty - create a new one
                price_levels[price] = {&order, &order};
                //std::cout << "ICEBERG order inserted in ladder" << std::endl;
            } else {
                // Add order to the end of the list
                it->second.tail->next = &orders[order_id];
                orders[order_id].prev = it->second.tail;
                it->second.tail = &orders[order_id];
            }
        }
        else {
            // Create new order
            Order& order = orders[order_id];
            order.order_id = order_id;
            order.timestamp = 0;
            order.quantity = quantity;
            order.display_quantity = quantity;
            order.filled_quantity = prematch_quantity - quantity;
            order.price = price;
            order.side = side;
            order.flags = flags;
            order.next = nullptr;
            order.prev = nullptr;
            
            auto it = price_levels.find(price);
            if (it == price_levels.end() || it->second.head == nullptr || it->second.tail == nullptr) {
                // Either price level doesn't exist or it's empty - create a new one
                price_levels[price] = {&order, &order};
                /*if (flags & static_cast<uint8_t>(oe::ORDER_FLAGS::AON)) {
                    //std::cout << "AON order inserted in ladder" << std::endl;
                }
                else {
                    //std::cout << "Regular order inserted in ladder" << std::endl;
                }*/
            } else {
                // Add order to the end of the list
                it->second.tail->next = &orders[order_id];
                orders[order_id].prev = it->second.tail;
                it->second.tail = &orders[order_id];
            }
        }   
    
        if (notify && !(flags & static_cast<uint8_t>(oe::ORDER_FLAGS::IOC))) {
            uint8_t subscriber_flags = flags;
            
            // Remove ICEBERG flag only for subscriber notification
            subscriber_flags = subscriber_flags & ~static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG);
            
            // For iceberg orders, only notify with the display quantity for visible portion
            uint32_t visible_qty = (is_iceberg) ? display_quantity : quantity;
            
            subscriber->onNewOrder(order_id, symbol, side, visible_qty, display_quantity, price, subscriber_flags);
        }
    }

    template <typename Subscriber>
    template <typename PriceLevels>
void OrderLadder<Subscriber>::handle_delete_order(uint64_t order_id, PriceLevels& price_levels) {
    auto it = orders.find(order_id);
    if (it == orders.end()) {
        subscriber->onCancelReject(order_id);
        return;
    }

    if (price_levels.find(it->second.price) == price_levels.end()) {
        throw std::runtime_error("Price level " + std::to_string(it->second.price) + " for this order "
            + std::to_string(order_id) + " does not exist");
    }

    int32_t price = it->second.price;
    PriceLevel& price_level = price_levels[price];
    Order& order = it->second;
    
    if (price_level.head == &order) {
        price_level.head = order.next;
    }

    if (price_level.tail == &order) {
        price_level.tail = order.prev;
    }

    if (order.prev) {
        order.prev->next = order.next;
    }

    if (order.next) {
        order.next->prev = order.prev;
    }

    orders.erase(order_id);
    
    // Check if price level is now empty and remove it if so
    if (price_level.head == nullptr && price_level.tail == nullptr) {
        price_levels.erase(price);
    }
}

    template <typename Subscriber>
    template <bool notify>
    void OrderLadder<Subscriber>::delete_order(uint64_t order_id) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            subscriber->onCancelReject(order_id);
            return;
        }
    
        //int32_t price = it->second.price;
        if (it->second.side == md::SIDE::BUY) {
            handle_delete_order(order_id, buy_levels);
            // No need to check and erase again since we do it in handle_delete_order
        } else {
            handle_delete_order(order_id, sell_levels);
            // No need to check and erase again since we do it in handle_delete_order
        }
    
        if (notify) {
            subscriber->onDeleteOrder(order_id);
        }
    }

    template <typename Subscriber>
    void OrderLadder<Subscriber>::modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            subscriber->onCancelReject(order_id);
            return;
        }

        if (it->second.filled_quantity >= quantity) {
            delete_order<true>(order_id);
            return;
        }

        if (it->second.price == price && it->second.side == side) {
            // price has not changed
            it->second.quantity = quantity;
            subscriber->onModifyOrder(order_id, symbol, side, quantity - it->second.filled_quantity, price);
            return;
        }

        delete_order<false>(order_id);
        new_order<false>(order_id, side, quantity, price, it->second.flags, it->second.filled_quantity);

        subscriber->onModifyOrder(order_id, symbol, side, quantity - it->second.filled_quantity, price);
    }
    template <typename Subscriber>
template <typename PriceLevels>
void OrderLadder<Subscriber>::match(uint64_t incoming_order_id, md::SIDE side, uint32_t& quantity, int32_t price, uint8_t flags,
                                  PriceLevels& opposite_side_price_levels) {
    // AON match logic                                   
    if (flags & static_cast<uint8_t>(oe::ORDER_FLAGS::AON)) {
        uint32_t available_liquidity = 0;
        auto it_check = opposite_side_price_levels.begin();

        while (it_check != opposite_side_price_levels.end()) {
            if ((side == md::SIDE::BUY && it_check->first > price) || 
                (side == md::SIDE::SELL && it_check->first < price)) {
                break;
            }

            PriceLevel& price_level = it_check->second;
            Order* order = price_level.head;
        
            while (order) {
                available_liquidity += order->quantity;
                order = order->next;
            }
        
            ++it_check;
        }

        if (available_liquidity < quantity) {
            if (flags & static_cast<uint8_t>(oe::ORDER_FLAGS::IOC)) {
                subscriber->onDeleteOrder(incoming_order_id, false);
            }
            return;
        }
    }
    
    // Process matching orders
    while (quantity > 0) {
        // Find the best price level that matches
        auto it = opposite_side_price_levels.begin();
        
        // Check if we've run out of price levels or reached an invalid price
        if (it == opposite_side_price_levels.end() || 
            (side == md::SIDE::BUY && it->first > price) || 
            (side == md::SIDE::SELL && it->first < price)) {
            break;
        }
        
        PriceLevel& price_level = it->second;
        
        // Skip empty price levels
        if (price_level.head == nullptr) {
            it = opposite_side_price_levels.erase(it);
            continue;
        }
        
        // Get the first order at this price level
        Order* order = price_level.head;
        bool is_iceberg = order->flags & static_cast<uint8_t>(oe::ORDER_FLAGS::ICEBERG);
        
        // Determine the matchable quantity
        uint32_t matchable_qty = is_iceberg ? order->display_quantity : order->quantity;
        
        if (is_iceberg) {
            // std::cout << "DEBUG: Iceberg order " << order->order_id 
            //           << " - display qty: " << matchable_qty 
            //           << ", total remaining: " << order->quantity 
            //           << ", flags: " << static_cast<int>(order->flags) << std::endl;
        }
        
        // Record all necessary information before modifying the order
        uint64_t order_id = order->order_id;
        int32_t order_price = order->price;
        md::SIDE order_side = order->side;
        uint32_t filled_quantity = order->filled_quantity;
        uint32_t total_quantity = order->quantity;
        uint32_t display_size = is_iceberg ? order->display_quantity : 0;
        uint8_t order_flags = order->flags;
        
        if (quantity >= matchable_qty) {
            // We can match the entire visible portion
            
            // Create trade and fill notifications
            subscriber->onTrade(order_id, matchable_qty, order_price);
            subscriber->onFill(order_id, symbol, order_side, matchable_qty, order_price,
                              static_cast<uint8_t>(oe::FILL_FLAGS::CLOSED));
            
            // Reduce remaining quantity
            quantity -= matchable_qty;
            
            // Notify about the incoming order fill
            subscriber->onFill(incoming_order_id, symbol, side, matchable_qty, order_price,
                              static_cast<uint8_t>(quantity > 0 ? oe::FILL_FLAGS::PARTIAL_FILL : oe::FILL_FLAGS::CLOSED));
            
            // Remove the order from the book
            handle_delete_order(order_id, opposite_side_price_levels);
            
            if (is_iceberg) {
                // For iceberg orders, check if there's quantity remaining after this fill
                uint32_t new_filled = filled_quantity + matchable_qty;
                uint32_t remaining = total_quantity - matchable_qty;
                
                if (remaining > 0) {
                    // There's still quantity remaining - we need to refresh
                    // std::cout << "DEBUG: Refreshing iceberg order " << order_id 
                    //           << " - remaining total: " << remaining 
                    //           << ", new display qty: " << std::min(remaining, display_size) << std::endl;
                    
                    // Re-add the order with updated quantities
                    new_order<true>(order_id, order_side, remaining, order_price, 
                                  order_flags, new_filled, display_size);
                } else {
                    // No remaining quantity
                    subscriber->onDeleteOrder(order_id, false);
                }
            } else {
                // For regular orders, just notify about deletion
                subscriber->onDeleteOrder(order_id, false);
            }
        } else {
            // We can only match part of the visible quantity
            subscriber->onTrade(order_id, quantity, order_price);
            subscriber->onFill(order_id, symbol, order_side, quantity, order_price,
                              static_cast<uint8_t>(oe::FILL_FLAGS::PARTIAL_FILL));
            subscriber->onFill(incoming_order_id, symbol, side, quantity, order_price,
                              static_cast<uint8_t>(oe::FILL_FLAGS::CLOSED));
            
            // std::cout << "DEBUG: Partial match for order " << order_id 
            //           << " - filled: " << quantity << std::endl;
            
            // Update the order quantities
            if (is_iceberg) {
                // Find the order again (it might have moved in memory due to unordered_map operations)
                auto order_it = orders.find(order_id);
                if (order_it != orders.end()) {
                    order_it->second.display_quantity -= quantity;
                    order_it->second.quantity -= quantity;
                    order_it->second.filled_quantity += quantity;
                    
                    // std::cout << "DEBUG: Iceberg after partial fill - display qty: " 
                    //           << order_it->second.display_quantity 
                    //           << ", total remaining: " << order_it->second.quantity << std::endl;
                }
            } else {
                // Find the order again
                auto order_it = orders.find(order_id);
                if (order_it != orders.end()) {
                    order_it->second.quantity -= quantity;
                    order_it->second.filled_quantity += quantity;
                }
            }
            
            // No more quantity to match
            quantity = 0;
            return;
        }
    }
}
}  // namespace ndfex
