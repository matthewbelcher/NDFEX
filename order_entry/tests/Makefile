CXX = g++
CXXFLAGS = -std=c++17 -I../../matching_engine -I../ -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lgtest -lgtest_main -pthread

SRC = test_oe_stream_parser.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = test_oe_stream_parser

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(OBJ): $(SRC) ../oe_stream_parser.H
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean