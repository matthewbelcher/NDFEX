CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I../spdlog/include -I../ -I../SPSCQueue -I../order_entry -I./
LDFLAGS = -pthread

SRC = md_snapshots.cpp snapshot_client.cpp snapshot_writer.cpp  ../matching_engine/utils.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = md_snapshots

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
