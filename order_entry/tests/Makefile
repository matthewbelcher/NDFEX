CXX = g++
CXXFLAGS = -std=c++17 -Wall -Werror -I../../matching_engine -I../../spdlog/include -I../ -I/usr/local/include -I../../
LDFLAGS = -L/usr/local/lib -lgtest -lgtest_main -pthread

SRC1 = test_oe_stream_parser.cpp
SRC2 = test_oe_client_handler.cpp
SRC_UTILS = ../../matching_engine/utils.cpp

OBJ1 = $(SRC1:.cpp=.o)
OBJ2 = $(SRC2:.cpp=.o)
OBJ_UTILS = $(SRC_UTILS:.cpp=.o)

TARGET_DIR = out

TARGET1 = $(TARGET_DIR)/test_oe_stream_parser
TARGET2 = $(TARGET_DIR)/test_oe_client_handler


all: $(TARGET1) $(TARGET2)

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

$(TARGET1): $(OBJ1) $(OBJ_UTILS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(TARGET2): $(OBJ2) $(OBJ_UTILS)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(OBJ1): $(SRC1) ../oe_stream_parser.H
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ2): $(SRC2) ../oe_client_handler.H
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_UTILS): $(SRC_UTILS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TARGET1) $(TARGET2)
	./$(TARGET1)
	./$(TARGET2)

clean:
	rm -f $(OBJ1) $(OBJ2) $(OBJ_UTILS) $(TARGET1) $(TARGET2)

.PHONY: all clean test