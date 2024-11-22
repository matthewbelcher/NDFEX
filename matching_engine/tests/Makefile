CXX = clang++
CXXFLAGS = -std=c++17 -I../ -I$(GTEST_DIR)/include -I../../SPSCQueue/ -I../../ -pthread
LDFLAGS = -L$(GTEST_DIR)/lib -lgtest -lgtest_main

GTEST_DIR = /usr/local

SRCS = test_matching_engine.cpp test_order_ladder.cpp test_spsc_md_queue.cpp ../order_ladder.cpp
OBJS = $(SRCS:.cpp=.o)

TARGETS = test_matching_engine test_order_ladder test_spsc_md_queue

all: $(TARGETS)

test_matching_engine: test_matching_engine.o ../order_ladder.o
	$(CXX) $(CXXFLAGS) -o test_matching_engine test_matching_engine.o ../order_ladder.o $(LDFLAGS)

test_order_ladder: test_order_ladder.o ../order_ladder.o
	$(CXX) $(CXXFLAGS) -o test_order_ladder test_order_ladder.o ../order_ladder.o $(LDFLAGS)

test_spsc_md_queue: test_spsc_md_queue.o
	$(CXX) $(CXXFLAGS) -o test_spsc_md_queue test_spsc_md_queue.o $(LDFLAGS)

%.o: %.cpp %.H
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGETS)

.PHONY: all clean
