CXX = clang++
CXXFLAGS = -std=c++17 -I../ -I$(GTEST_DIR)/include -I../../SPSCQueue/ -I../../ -pthread
LDFLAGS = -L$(GTEST_DIR)/lib -lgtest -lgtest_main

GTEST_DIR = /usr/local

SRCS = test_matching_engine.cpp test_order_ladder.cpp test_spsc_md_queue.cpp test_md_mcast.cpp ../order_ladder.cpp ../md_mcast.cpp ../spsc_md_queue.cpp
OBJS = $(SRCS:.cpp=.o)

TARGETS = test_matching_engine test_order_ladder test_spsc_md_queue test_md_mcast
TARGET_DIR = out

all: $(TARGETS)

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

test_matching_engine: $(TARGET_DIR) test_matching_engine.o ../order_ladder.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_DIR)/test_matching_engine test_matching_engine.o ../order_ladder.o $(LDFLAGS)

test_order_ladder: $(TARGET_DIR) test_order_ladder.o ../order_ladder.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_DIR)/test_order_ladder test_order_ladder.o ../order_ladder.o $(LDFLAGS)

test_spsc_md_queue: $(TARGET_DIR) test_spsc_md_queue.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_DIR)/test_spsc_md_queue test_spsc_md_queue.o $(LDFLAGS)

test_md_mcast: $(TARGET_DIR) test_md_mcast.o ../md_mcast.o ../order_ladder.o
	$(CXX) $(CXXFLAGS) -o $(TARGET_DIR)/test_md_mcast test_md_mcast.o ../md_mcast.o ../order_ladder.o $(LDFLAGS)

%.o: %.cpp %.H
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGETS)
	rm -rf $(TARGET_DIR)

.PHONY: all clean
