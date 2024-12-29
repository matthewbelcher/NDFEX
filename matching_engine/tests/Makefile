CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Werror -I../ -I$(GTEST_DIR)/include -I../../spdlog/include -I../../SPSCQueue/ -I../../ -pthread
LDFLAGS = -L$(GTEST_DIR)/lib -lgtest -lgtest_main

GTEST_DIR = /usr/local

SRCS = test_order_ladder.cpp test_md_mcast.cpp  ../md_mcast.cpp ../spsc_md_queue.cpp ../utils.cpp
OBJS = $(SRCS:.cpp=.o)

TARGETS = test_order_ladder test_md_mcast
TARGET_DIR = out

all: $(TARGETS)

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

test_order_ladder: $(TARGET_DIR) test_order_ladder.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_DIR)/test_order_ladder test_order_ladder.o  $(LDFLAGS)

test_order_ladder.o: test_order_ladder.cpp ../order_ladder.H
	$(CXX) $(CXXFLAGS) -c test_order_ladder.cpp -o test_order_ladder.o

test_md_mcast: $(TARGET_DIR) test_md_mcast.o ../md_mcast.o  ../utils.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_DIR)/test_md_mcast test_md_mcast.o ../md_mcast.o ../utils.o $(LDFLAGS)

test: $(TARGETS)
	$(TARGET_DIR)/test_order_ladder
	$(TARGET_DIR)/test_md_mcast

%.o: %.cpp %.H ../order_ladder.H
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGETS)
	rm -rf $(TARGET_DIR)

.PHONY: all clean test
