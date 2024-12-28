CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Werror -I../spdlog/include -I../ -I../SPSCQueue -I../order_entry -I./
LDFLAGS = -pthread

SRC = matching_engine.cpp ../order_entry/oe_server.cpp md_mcast.cpp ../order_entry/oe_server.cpp utils.cpp ../order_entry/oe_validator.cpp ../order_entry/oe_client_handler.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = out/matching_engine

all: release

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

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET)_debug
	rm -rf out/

.PHONY: all clean release debug