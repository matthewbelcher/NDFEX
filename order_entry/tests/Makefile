CXX = g++
CXXFLAGS = -std=c++17 -Wall -Werror -I../../matching_engine -I../../spdlog/include -I../ -I/usr/local/include -I../../ -I../../SPSCQueue
LDFLAGS = -L/usr/local/lib -lgtest -lgtest_main -pthread

SRC_DIR = ../..
SRC1 = $(SRC_DIR)/order_entry/tests/test_oe_stream_parser.cpp
SRC2 = $(SRC_DIR)/order_entry/tests/test_oe_client_handler.cpp
COMMON_SRC = $(SRC_DIR)/matching_engine/utils.cpp $(SRC_DIR)/order_entry/oe_validator.cpp $(SRC_DIR)/order_entry/oe_client_handler.cpp

OBJ_DIR = out/obj
OBJ1 = $(SRC1:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o) $(COMMON_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
OBJ2 = $(SRC2:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o) $(COMMON_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

TARGET_DIR = out
TARGET1 = $(TARGET_DIR)/test_oe_stream_parser
TARGET2 = $(TARGET_DIR)/test_oe_client_handler

all: $(TARGET_DIR) $(OBJ_DIR) $(TARGET1) $(TARGET2)

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/order_entry/tests
	mkdir -p $(OBJ_DIR)/matching_engine

$(TARGET1): $(OBJ1)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(TARGET2): $(OBJ2)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TARGET1) $(TARGET2)
	$(TARGET1)
	$(TARGET2)

clean:
	rm -f $(OBJ1) $(OBJ2) $(TARGET1) $(TARGET2)

	rm -rf $(OBJ_DIR) $(TARGET_DIR)

.PHONY: all clean test
