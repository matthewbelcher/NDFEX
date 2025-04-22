CXX = clang++
CXXFLAGS = -std=c++17 -Wall -I../../ -I../../spdlog/include -I../../SPSCQueue
LDFLAGS = -pthread

# Test targets
TARGETS = aon_tests iceberg_tests combined_tests

all: $(TARGETS)

aon_tests: unit_test_aon.cpp mock_subscriber.cpp order_ladder_wrapper.cpp test_framework.H
	$(CXX) $(CXXFLAGS) -o $@ unit_test_aon.cpp mock_subscriber.cpp order_ladder_wrapper.cpp $(LDFLAGS)

iceberg_tests: unit_test_iceberg.cpp mock_subscriber.cpp order_ladder_wrapper.cpp test_framework.H
	$(CXX) $(CXXFLAGS) -o $@ unit_test_iceberg.cpp mock_subscriber.cpp order_ladder_wrapper.cpp $(LDFLAGS)

combined_tests: unit_test_combined.cpp mock_subscriber.cpp order_ladder_wrapper.cpp test_framework.H
	$(CXX) $(CXXFLAGS) -o $@ unit_test_combined.cpp mock_subscriber.cpp order_ladder_wrapper.cpp $(LDFLAGS)

clean:
	rm -f *.o $(TARGETS)

run: all
	./iceberg_tests
	./aon_tests
	./combined_tests

.PHONY: all clean run