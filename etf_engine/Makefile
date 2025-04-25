CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Werror -Iinclude
LDFLAGS = -pthread

PISTACHE_INCLUDE = /usr/local/include
PISTACHE_LIB = /usr/local/lib

ENGINE_SRC = main.cpp etf_engine.cpp etf_bookkeeping.cpp
ENGINE_OBJ = $(ENGINE_SRC:.cpp=.o)
ENGINE_DEPS = etf_engine.hpp etf_bookkeeping.hpp
ENGINE_TARGET = etf_engine_app

TEST_SRC = etf_tests/test_etf_bookkeeping.cpp etf_bookkeeping.cpp
TEST_OBJ = $(TEST_SRC:.cpp=.o)
TEST_DEPS = etf_bookkeeping.hpp etf_tests/test_framework.H
TEST_TARGET = etf_bookkeeping_tests

all: $(ENGINE_TARGET)

tests: $(TEST_TARGET)

$(ENGINE_TARGET): $(ENGINE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_etf_bookkeeping.o: test_etf_bookkeeping.cpp etf_bookkeeping.hpp etf_bookkeeping.cpp
	$(CXX) $(CXXFLAGS) -c test_etf_bookkeeping.cpp -o test_etf_bookkeeping.o

test_etf: test_etf_bookkeeping.o etf_bookkeeping.o
	$(CXX) $(CXXFLAGS) -o test_etf test_etf_bookkeeping.o etf_bookkeeping.o $(LDFLAGS)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(ENGINE_OBJ) $(TEST_OBJ) $(ENGINE_TARGET) $(TEST_TARGET) test_etf

.PHONY: all clean