CXX = clang++
CXXFLAGS = -g -std=c++17 -Wall -Wextra -I/usr/local/include -I../ -I../spdlog/include -I../websocketpp/
LDFLAGS = -L/usr/local/lib -lpthread

TARGET = web_data
SRCS = web_data.cpp ../bots/md_client.cpp ../bots/order_book.cpp clearing_client.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean