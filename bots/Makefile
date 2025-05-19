CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Werror -I../spdlog/include -I../ -I../SPSCQueue -I../order_entry -I./
LDFLAGS = -pthread

SRC = bot_runner.cpp oe_client.cpp md_client.cpp order_book.cpp random_walk_fair_value.cpp fair_value_mm.cpp ../matching_engine/utils.cpp random_taker.cpp fair_value_stack_mm.cpp\
 	stack_manager.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = out/bot_runner

PRINT_SNAPSHOTS_SRC = print_snapshots.cpp
PRINT_SNAPSHOTS_OBJ = $(PRINT_SNAPSHOTS_SRC:.cpp=.o)
PRINT_SNAPSHOTS_TARGET = out/print_snapshots

STABLE_SRC = stable_bot_runner.cpp oe_client.cpp md_client.cpp order_book.cpp constant_fair_value.cpp fair_value_mm.cpp ../matching_engine/utils.cpp random_taker.cpp
STABLE_OBJ = $(STABLE_SRC:.cpp=.o)
STABLE_TARGET = out/stable_bot_runner

REJECT_SRC = reject_bot.cpp oe_client.cpp md_client.cpp order_book.cpp random_walk_fair_value.cpp fair_value_mm.cpp ../matching_engine/utils.cpp random_taker.cpp
REJECT_OBJ = $(REJECT_SRC:.cpp=.o)
REJECT_TARGET = out/reject_bot

SMARTER_SRC = smarter_bots.cpp oe_client.cpp md_client.cpp order_book.cpp random_walk_fair_value.cpp fair_value_mm.cpp ../matching_engine/utils.cpp random_taker.cpp\
 	stack_manager.cpp fair_value_stack_mm.cpp imbalance_taker.cpp pressure_taker.cpp resting_orders.cpp
SMARTER_OBJ = $(SMARTER_SRC:.cpp=.o)
SMARTER_TARGET = out/smarter_bots

all: release print_snapshots stable_bot_runner reject_bot smarter_bots

release: CXXFLAGS += -O3
release: $(TARGET)

debug: CXXFLAGS += -g -O0
debug: $(TARGET)_debug

$(TARGET): $(OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET)_debug: $(OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(PRINT_SNAPSHOTS_TARGET): $(PRINT_SNAPSHOTS_OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(STABLE_TARGET): $(STABLE_OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -O3 -o $@ $^ $(LDFLAGS)

$(REJECT_TARGET): $(REJECT_OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -O3 -o $@ $^ $(LDFLAGS)

$(SMARTER_TARGET): $(SMARTER_OBJ)
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -O3 -o $@ $^ $(LDFLAGS)


print_snapshots: $(PRINT_SNAPSHOTS_TARGET)

stable_bot_runner: $(STABLE_TARGET)

reject_bot: $(REJECT_TARGET)

smarter_bots: $(SMARTER_TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET)_debug $(PRINT_SNAPSHOTS_OBJ) $(PRINT_SNAPSHOTS_TARGET) $(STABLE_OBJ) $(STABLE_TARGET) $(REJECT_OBJ) $(REJECT_TARGET) $(SMARTER_OBJ) $(SMARTER_TARGET)
	rm -rf out/

.PHONY: all clean release debug print_snapshots stable_bot_runner reject_bot smarter_bots