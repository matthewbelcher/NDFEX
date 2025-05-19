#include "stack_manager.H"

#include <matching_engine/utils.H>

namespace ndfex::bots {

  StackManager::StackManager(OEClient& oe,
                     symbol_definition symbol,
                     uint32_t width_in_ticks,
                     uint32_t quantity,
                     uint32_t stack_depth,
                     uint32_t& last_order_id,
                     std::shared_ptr<spdlog::logger> logger)
    : oe(oe), symbol(symbol), width_in_ticks(width_in_ticks), quantity(quantity), stack_depth(stack_depth),
      last_order_id(last_order_id), logger(logger) {

    }

  void StackManager::process(int32_t fair_value, int32_t bid_price, int32_t ask_price) {
    // stack orders width_in_ticks away from the fair value but not more than the bid/ask price

    // round to the nearest tick size
    int32_t desired_bid_price = static_cast<int32_t>(fair_value - width_in_ticks);
    desired_bid_price = (desired_bid_price / symbol.tick_size) * symbol.tick_size;

    int32_t desired_ask_price = static_cast<int32_t>(fair_value + width_in_ticks);
    desired_ask_price = ( (desired_ask_price - 1) / symbol.tick_size) * symbol.tick_size;

    int32_t bid_price_to_stack = std::min(desired_bid_price, bid_price);
    int32_t ask_price_to_stack = std::max(desired_ask_price, ask_price);

    if (bid_price_to_stack < symbol.min_price) {
      bid_price_to_stack = symbol.min_price;
    }
    if (ask_price_to_stack > symbol.max_price) {
      ask_price_to_stack = symbol.max_price;
    }

    if (bid_price_to_stack >= ask_price_to_stack) {
      logger->warn("Bid price {} is greater than or equal to ask price {}", bid_price_to_stack, ask_price_to_stack);
      return;
    }

    // stack bid orders
    if (bid_price_to_stack > 0) {
      stack_side(bid_price_to_stack, bid_orders, md::SIDE::BUY);
    }
    // stack ask orders
    if (ask_price_to_stack > 0) {
      stack_side(ask_price_to_stack, ask_orders, md::SIDE::SELL);
    }
  }

  int32_t StackManager::improve(int32_t price, md::SIDE side, uint32_t ticks) {
    if (side == md::SIDE::BUY) {
      return price + (width_in_ticks * ticks);
    } else {
      return price - (width_in_ticks * ticks);
    }
  }

  int32_t StackManager::worsen(int32_t price, md::SIDE side, uint32_t ticks) {
    if (side == md::SIDE::BUY) {
      return price - (width_in_ticks * ticks);
    } else {
      return price + (width_in_ticks * ticks);
    }
  }

  template <typename SideOrders>
  void StackManager::stack_side(int32_t price, SideOrders& orders, md::SIDE side) {
    // cancel any orders ahead of the price
    for (auto it = orders.begin(); it != orders.end(); ) {
      if ((side == md::SIDE::BUY && it->price > price) || (side == md::SIDE::SELL && it->price < price)) {
        logger->info("Canceling order {} at price {} for side {}", it->order_id, it->price, (side == md::SIDE::BUY ? "bid" : "ask"));
        oe.cancel_order(it->order_id);
        it = orders.erase(it);
      } else {
        ++it;
      }
    }
    // check if the order is already in the stack
    auto it = orders.find(open_order(0, price, 0));
    if (it != orders.end()) {
      // order already exists, do nothing
      return;
    }

    // check if the stack is full
    if (orders.size() >= stack_depth) {
      // remove the worst order from the stack
      auto worst_order = *orders.rbegin();
      // modify this order to the new price
      logger->info("Modifying worst order {} at price {} to price {} for side {}", worst_order.order_id, worst_order.price, price,
         (side == md::SIDE::BUY ? "bid" : "ask"));
      oe.modify_order(worst_order.order_id, side, quantity, price);
      last_order_send_ts = nanotime();

      worst_order.price = price;
      worst_order.quantity = quantity;

      // reinsert the modified order at the new price
      orders.erase(--orders.end());
      orders.insert(worst_order);

    } else if (last_order_send_ts == 0 || ((nanotime() - last_order_send_ts) > 100000000)) {
      // create a new order
      uint32_t order_id = last_order_id++;
      open_order new_order(order_id, price, quantity);
      orders.insert(new_order);

      oe.send_order(symbol.symbol, order_id, side, quantity, price, 0);
      last_order_send_ts = nanotime();
      logger->info("Stacked {} order: id {}, price {}, quantity {}", (side == md::SIDE::BUY ? "bid" : "ask"), order_id, price, quantity);
    }
  }

  void StackManager::cancel_all_orders() {
    for (const auto& order : bid_orders) {
      logger->info("Canceling order {} at price {} for side bid", order.order_id, order.price);
      oe.cancel_order(order.order_id);
    }

    for (const auto& order : ask_orders) {
      logger->info("Canceling order {} at price {} for side bid", order.order_id, order.price);
      oe.cancel_order(order.order_id);
    }
  }

} // namespace ndfex::bots