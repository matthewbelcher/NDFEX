CXX = clang++
CXXFLAGS = -g -std=c++17 -Wall -Wextra -I/usr/local/include -I../ -I../spdlog/include -I../FTXUI/include
LDFLAGS = -L/usr/local/lib -L../FTXUI/build -lftxui-dom -lftxui-screen -lftxui-component -lpthread

TARGET = md_viewer
SRCS = md_viewer.cpp ../bots/md_client.cpp ../bots/order_book.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
