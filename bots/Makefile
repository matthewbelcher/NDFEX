CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Werror -I../spdlog/include -I../ -I../SPSCQueue -I../order_entry -I./
LDFLAGS = -pthread

SRC = bot_runner.cpp oe_client.cpp md_client.cpp order_book.cpp fair_value.cpp fair_value_mm.cpp ../matching_engine/utils.cpp random_taker.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = out/bot_runner

PRINT_SNAPSHOTS_SRC = print_snapshots.cpp
PRINT_SNAPSHOTS_OBJ = $(PRINT_SNAPSHOTS_SRC:.cpp=.o)
PRINT_SNAPSHOTS_TARGET = out/print_snapshots

all: release print_snapshots

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

print_snapshots: $(PRINT_SNAPSHOTS_TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET)_debug $(PRINT_SNAPSHOTS_OBJ) $(PRINT_SNAPSHOTS_TARGET)
	rm -rf out/

.PHONY: all clean release debug print_snapshots