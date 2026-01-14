#pragma once

#include <cstdint>
#include <unordered_map>
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>
#include <iostream>

#include "market_data/md_protocol.H"
#include "order_entry/oe_protocol.H"
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
            uint32_t filled_quantity;
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
        static const std::size_t ORDER_POOL_BLOCK_SIZE = 4096;

        class OrderPool {
        public:
            Order* allocate() {
                if (free_list.empty()) {
                    add_block();
                }
                Order* order = free_list.back();
                free_list.pop_back();
                return order;
            }

            void deallocate(Order* order) {
                if (!order) {
                    return;
                }
                order->next = nullptr;
                order->prev = nullptr;
                free_list.push_back(order);
            }

        private:
            void add_block() {
                blocks.emplace_back(std::make_unique<Order[]>(ORDER_POOL_BLOCK_SIZE));
                Order* block = blocks.back().get();
                free_list.reserve(free_list.size() + ORDER_POOL_BLOCK_SIZE);
                for (std::size_t i = 0; i < ORDER_POOL_BLOCK_SIZE; ++i) {
                    free_list.push_back(&block[i]);
                }
            }

            std::vector<std::unique_ptr<Order[]>> blocks;
            std::vector<Order*> free_list;
        };

        std::unordered_map<uint64_t, Order*> orders{INITIAL_ORDER_CAPACITY};
        std::map<int32_t, PriceLevel, std::greater<int32_t>> buy_levels;
        std::map<int32_t, PriceLevel, std::less<int32_t>> sell_levels;
        OrderPool order_pool;

        Subscriber* const subscriber;
        const uint32_t symbol;

    public:
        OrderLadder(Subscriber* subscriber, uint32_t symbol) : subscriber(subscriber), symbol(symbol) {};

        ~OrderLadder() = default;

        template <bool notify = true>
        void new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags, uint32_t filled_quantity = 0);

        template <bool notify = true>
        void delete_order(uint64_t order_id);

        void modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price);

    private:

        template <typename PriceLevels>
        void match(uint64_t order_id, md::SIDE side, uint32_t& quantity, int32_t price,
                   PriceLevels& opposite_side_price_levels);

        template <typename PriceLevels>
        void handle_new_order(uint64_t order_id, md::SIDE side, uint32_t& quantity, int32_t price, uint8_t flags, PriceLevels& price_levels,
                              bool notify, uint32_t filled_quantity);

        template <typename PriceLevels>
        void handle_delete_order(uint64_t order_id, PriceLevels& price_levels);
    };

    template <typename Subscriber>
    template <bool notify>
    void OrderLadder<Subscriber>::new_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price, uint8_t flags,
                                            uint32_t filled_quantity) {
        if (side == md::SIDE::BUY) {
            handle_new_order(order_id, side, quantity, price, flags, buy_levels, notify, filled_quantity);
        } else {
            handle_new_order(order_id, side, quantity, price, flags, sell_levels, notify, filled_quantity);
        }
    }

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::handle_new_order(uint64_t order_id, md::SIDE side, uint32_t& quantity, int32_t price, uint8_t flags,
                                                   PriceLevels& price_levels, bool notify, uint32_t filled_quantity) {
        if (quantity > filled_quantity) {
            quantity = quantity - filled_quantity;
        } else {
            throw std::runtime_error("filled quantity > quantity on new order!");
        }

        uint32_t prematch_quantity = quantity;
        if (side == md::SIDE::BUY) {
            match(order_id, side, quantity, price, sell_levels);
        } else {
            match(order_id, side, quantity, price, buy_levels);
        }

        if (notify && !(flags & static_cast<uint8_t>(oe::ORDER_FLAGS::IOC))) {
            subscriber->onNewOrder(order_id, symbol, side, quantity, price, flags);
        }

        // IOC orders are not added to the ladder
        if (quantity > 0 && (flags & static_cast<uint8_t>(oe::ORDER_FLAGS::IOC))) {
            if (notify) {
                // still need to notify sender that it was closed
                subscriber->onDeleteOrder(order_id, false);
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

        // create new order
        Order* order_ptr = order_pool.allocate();
        order_ptr->order_id = order_id;
        order_ptr->timestamp = 0;
        order_ptr->quantity = quantity;
        order_ptr->filled_quantity = prematch_quantity - quantity;
        order_ptr->price = price;
        order_ptr->side = side;
        order_ptr->flags = flags;
        order_ptr->next = nullptr;
        order_ptr->prev = nullptr;

        auto it = price_levels.find(price);
        if (it == price_levels.end()) {
            // insert order into the ladder
            price_levels.insert({price, {order_ptr, order_ptr}});

        } else {
            // add order to the end of the list
            if (it->second.tail == nullptr) {
                throw std::runtime_error("Price level " + std::to_string(price) + " tail is nullptr");
            }

            it->second.tail->next = order_ptr;
            order_ptr->prev = it->second.tail;
            it->second.tail = order_ptr;
        }

        orders.emplace(order_id, order_ptr);
    }

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::handle_delete_order(uint64_t order_id, PriceLevels& price_levels) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            subscriber->onCancelReject(order_id);
            return;
        }

        if (price_levels.find(it->second->price) == price_levels.end()) {
            throw std::runtime_error("Price level " + std::to_string(it->second->price) + " for this order "
                + std::to_string(order_id) + " does not exist");
        }

        PriceLevel& price_level = price_levels[it->second->price];
        Order& order = *it->second;
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
        order_pool.deallocate(&order);
    }

    template <typename Subscriber>
    template <bool notify>
    void OrderLadder<Subscriber>::delete_order(uint64_t order_id) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            subscriber->onCancelReject(order_id);
            return;
        }

        int32_t price = it->second->price;
        if (it->second->side == md::SIDE::BUY) {
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

        if (notify) {
            subscriber->onDeleteOrder(order_id, true);
        }
    }

    template <typename Subscriber>
    void OrderLadder<Subscriber>::modify_order(uint64_t order_id, md::SIDE side, uint32_t quantity, int32_t price) {
        auto it = orders.find(order_id);
        if (it == orders.end()) {
            subscriber->onCancelReject(order_id);
            return;
        }

        if (it->second->filled_quantity >= quantity) {
            delete_order<true>(order_id);
            return;
        }

        if (it->second->price == price && it->second->side == side) {
            // price has not changed
            it->second->quantity = quantity;
            subscriber->onModifyOrder(order_id, symbol, side, quantity - it->second->filled_quantity, price);
            return;
        }

        uint32_t pre_modify_filled_quantity = it->second->filled_quantity;
        uint8_t flags = it->second->flags;

        delete_order<false>(order_id);
        if (side == md::SIDE::BUY) {
            handle_new_order(order_id, side, quantity, price, flags, buy_levels, false, pre_modify_filled_quantity);
        } else {
            handle_new_order(order_id, side, quantity, price, flags, sell_levels, false, pre_modify_filled_quantity);
        }

        if (quantity > 0) {
            // order is not fully matched
            subscriber->onModifyOrder(order_id, symbol, side, quantity, price);
        } else {
            // order is fully matched, close it
            subscriber->onDeleteOrder(order_id, true);
        }
    }

    template <typename Subscriber>
    template <typename PriceLevels>
    void OrderLadder<Subscriber>::match(uint64_t incoming_order_id, md::SIDE side, uint32_t& quantity, int32_t price,
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
                    subscriber->onFill(order->order_id, symbol, order->side, order->quantity, order->price,
                     static_cast<uint8_t>(oe::FILL_FLAGS::CLOSED), false);

                    quantity -= order->quantity;

                    subscriber->onFill(incoming_order_id, symbol, side, order->quantity, order->price,
                                       static_cast<uint8_t>(quantity > 0 ? oe::FILL_FLAGS::PARTIAL_FILL : oe::FILL_FLAGS::CLOSED),
                                    true);

                    auto to_delete = order;
                    order = order->next;
                    handle_delete_order(to_delete->order_id, opposite_side_price_levels);
                } else {
                    // order is partially matched
                    subscriber->onTrade(order->order_id, quantity, order->price);
                    subscriber->onFill(order->order_id, symbol, order->side, quantity, order->price,
                                       static_cast<uint8_t>(oe::FILL_FLAGS::PARTIAL_FILL), false);
                    subscriber->onFill(incoming_order_id, symbol, side, quantity, order->price,
                                       static_cast<uint8_t>(oe::FILL_FLAGS::CLOSED), true);

                    order->filled_quantity += quantity;
                    order->quantity -= quantity;
                    quantity = 0;
                    return;
                }
            }

            if (price_level.head == nullptr) {
                it = opposite_side_price_levels.erase(it);
            } else if (quantity > 0) {
                // if this price level is not empty, but we still had quantity left, we should have fully matched
                throw std::runtime_error("Price level " + std::to_string(it->first) +
                " not empty but no orders? Impossible. Remaining quantity: " + std::to_string(quantity));
            }
        }
    }

}  // namespace ndfex
