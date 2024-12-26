#pragma once

#include <cstdint>
#include <unordered_map>
#include <map>
#include <stdexcept>
#include <iostream>

#include "market_data/md_protocol.H"
#include "exceptions.H"

namespace ndfex {

    template <typename Subscriber>
    class OrderLadder {

    public:

        class Order {
        public:
            uint64_t order_id;
            uint64_t timestamp;
            uint32_t quantity;
            uint32_t price;
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
        std::map<uint32_t, PriceLevel, std::greater<uint32_t>> buy_levels;
        std::map<uint32_t, PriceLevel, std::less<uint32_t>> sell_levels;

        Subscriber* const subscriber;
        const uint32_t symbol;

    public:
        OrderLadder(Subscriber* subscriber, uint32_t symbol) : subscriber(subscriber), symbol(symbol) {};

        ~OrderLadder() = default;

        void new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags);

        void delete_order(uint64_t order_id);

        void modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price);

    private:

        template <typename PriceLevels>
        void match(md::SIDE side, uint32_t& quantity, uint32_t price,
                   PriceLevels& opposite_side_price_levels);

        template <typename PriceLevels>
        void handle_new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags, PriceLevels& price_levels);

        template <typename PriceLevels>
        void handle_delete_order(uint64_t order_id, PriceLevels& price_levels);
    };

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::handle_new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags, PriceLevels& price_levels) {

        if (side == md::SIDE::BUY) {
            match(side, quantity, price, sell_levels);
        } else {
            match(side, quantity, price, buy_levels);
        }

        if (quantity == 0) {
            // order is fully matched
            return;
        }

        if (orders.find(order_id) != orders.end()) {
            // order already exists
            throw std::runtime_error("Order already exists");
        }

        // create new order
        Order& order = orders[order_id]; // create new order
        order.order_id = order_id;
        order.timestamp = 0;
        order.quantity = quantity;
        order.price = price;
        order.side = side;
        order.flags = flags;
        order.next = nullptr;
        order.prev = nullptr;

        auto it = price_levels.find(price);
        if (it == price_levels.end()) {
            // insert order into the ladder
            price_levels.insert({price, {&order, &order}});

        } else {
            // add order to the end of the list
            it->second.tail->next = &orders[order_id];
            orders[order_id].prev = it->second.tail;
            it->second.tail = &orders[order_id];
        }

        subscriber->onNewOrder(order_id, symbol, side, quantity, price, flags);
    }

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::handle_delete_order(uint64_t order_id, PriceLevels& price_levels) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            // order does not exist
            throw OrderNotFoundException("Order " + std::to_string(order_id) + " does not exist");
        }

        PriceLevel& price_level = price_levels[it->second.price];
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
        subscriber->onDeleteOrder(order_id);
    }

    template <typename Subscriber>
    void OrderLadder<Subscriber>::new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price, uint8_t flags) {
        if (side == md::SIDE::BUY) {
            handle_new_order(order_id, side, quantity, price, flags, buy_levels);
        } else {
            handle_new_order(order_id, side, quantity, price, flags, sell_levels);
        }
    }

    template <typename Subscriber>
    void OrderLadder<Subscriber>::delete_order(uint64_t order_id) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            // order does not exist
            throw OrderNotFoundException("Order " + std::to_string(order_id) + " does not exist");
        }

        uint32_t price = it->second.price;
        if (it->second.side == md::SIDE::BUY) {
            handle_delete_order(order_id, buy_levels);
            if (buy_levels.find(price) != buy_levels.end() && buy_levels[price].head == nullptr) {
                buy_levels.erase(price);
            }
        } else {
            handle_delete_order(order_id, sell_levels);
            if (sell_levels.find(price) != sell_levels.end() && sell_levels[price].head == nullptr) {
                sell_levels.erase(price);
            }
        }
    }

    template <typename Subscriber>
    void OrderLadder<Subscriber>::modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, uint32_t price) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            // order does not exist
            throw OrderNotFoundException("Order " + std::to_string(order_id) + " does not exist");
        }

        if (it->second.price == price && it->second.side == side) {
            // price has not changed
            it->second.quantity = quantity;
            return;
        }

        delete_order(order_id);
        new_order(order_id, side, quantity, price, it->second.flags);
    }

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::match(md::SIDE side, uint32_t& quantity, uint32_t price,
                                        PriceLevels& opposite_side_price_levels) {
        // check if there are any orders on the opposite side at the same or better price level
        auto it = opposite_side_price_levels.begin();
        while (it != opposite_side_price_levels.end() && quantity > 0) {
            if ((side == md::SIDE::BUY && it->first > price) || (side == md::SIDE::SELL && it->first < price)) {
                break;
            }

            PriceLevel& price_level = it->second;
            Order* order = price_level.head;
            while (order && quantity > 0) {
                if (order->quantity <= quantity) {
                    // order is fully matched
                    subscriber->onTrade(order->order_id, order->quantity, order->price);
                    quantity -= order->quantity;
                    auto to_delete = order;
                    order = order->next;
                    handle_delete_order(to_delete->order_id, opposite_side_price_levels);
                } else {
                    // order is partially matched
                    subscriber->onTrade(order->order_id, quantity, order->price);
                    order->quantity -= quantity;
                    quantity = 0;
                    return;
                }
            }

            if (price_level.head == nullptr) {
                it = opposite_side_price_levels.erase(it);
            } else {
                throw std::runtime_error("Price level " + std::to_string(it->first) + " not empty but no orders?");
            }
        }
    }

}  // namespace ndfex
