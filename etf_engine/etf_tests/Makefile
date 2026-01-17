
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -I../../ -I../../spdlog/include -I../../SPSCQueue
LDFLAGS = -pthread
# Paths to source files in parent directory
SRC = ../etf_bookkeeping.cpp
HEADERS = ../etf_bookkeeping.hpp ../etf_engine.hpp

# Test file
TEST_SRC = unit_test_bookkeeping.cpp
TEST_OBJ = unit_test_bookkeeping.o
TEST_TARGET = test_etf

# Rule to build the test binary
all: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJ) $(SRC:.cpp=.o)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_OBJ): $(TEST_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $(TEST_SRC) -o $(TEST_OBJ)

# Clean
clean:
	rm -f *.o $(TEST_TARGET)

run: all
	./unit_test_bookkeeping

.PHONY: all clean run
