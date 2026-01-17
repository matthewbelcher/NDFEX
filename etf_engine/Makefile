
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Werror -Iinclude
LDFLAGS = -pthread

ENGINE_SRC = main.cpp etf_engine.cpp etf_bookkeeping.cpp  http_server.cpp
ENGINE_OBJ = $(ENGINE_SRC:.cpp=.o)
ENGINE_DEPS = etf_engine.hpp etf_bookkeeping.hpp  http_server.hpp
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


%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(ENGINE_OBJ) $(TEST_OBJ) $(ENGINE_TARGET) $(TEST_TARGET) html_server.o

.PHONY: all clean

# 
#CXX = clang++
#CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Werror -Iinclude
#LDFLAGS = -pthread
##
#ENGINE_SRC = main.cpp etf_engine.cpp etf_bookkeeping.cpp html_server.cpp
#ENGINE_OBJ = $(ENGINE_SRC:.cpp=.o)
#ENGINE_DEPS = etf_engine.hpp etf_bookkeeping.hpp html_server.hpp
#ENGINE_TARGET = etf_engine_app
#
#TEST_SRC = etf_tests/test_etf_bookkeeping.cpp etf_bookkeeping.cpp
#TEST_OBJ = $(TEST_SRC:.cpp=.o)
#TEST_DEPS = etf_bookkeeping.hpp etf_tests/test_framework.H
#TEST_TARGET = etf_bookkeeping_tests
#
#all: $(ENGINE_TARGET)
#
#tests: $(TEST_TARGET)
#
#$(ENGINE_TARGET): $(ENGINE_OBJ)
#	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
#
#$(TEST_TARGET): $(TEST_OBJ)
#	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
#
#test_etf_bookkeeping.o: test_etf_bookkeeping.cpp etf_bookkeeping.hpp etf_bookkeeping.cpp
#	$(CXX) $(CXXFLAGS) -c test_etf_bookkeeping.cpp -o test_etf_bookkeeping.o
#
#test_etf: test_etf_bookkeeping.o etf_bookkeeping.o
#	$(CXX) $(CXXFLAGS) -o test_etf test_etf_bookkeeping.o etf_bookkeeping.o $(LDFLAGS)
#html_server.o: html_server.cpp html_server.hpp
#	$(CXX) $(CXXFLAGS) -c html_server.cpp -o html_server.o
#%.o: %.cpp
#	$(CXX) $(CXXFLAGS) -c $< -o $@
#
#
#clean:
#	rm -f $(ENGINE_OBJ) $(TEST_OBJ) $(ENGINE_TARGET) $(TEST_TARGET) test_etf
#
#.PHONY: all clean