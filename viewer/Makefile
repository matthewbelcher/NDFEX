CXX = clang++
CXXFLAGS = -g -std=c++17 -Wall -Wextra -I/usr/local/include -I../ -I../spdlog/include -I../FTXUI/include
LDFLAGS = -L/usr/local/lib -L../FTXUI/build -lftxui-dom -lftxui-screen -lftxui-component -lpthread -lpcap

TARGET = md_viewer
SRCS = md_viewer.cpp ../bots/md_client.cpp ../bots/order_book.cpp
OBJS = $(SRCS:.cpp=.o)

PCAP_TARGET = pcap_printer
PCAP_SRCS = pcap_printer.cpp ../bots/md_client.cpp ../bots/order_book.cpp
PCAP_OBJS = $(PCAP_SRCS:.cpp=.o)

BBO_TARGET = bbo_printer
BBO_SRCS = bbo_printer.cpp ../bots/md_client.cpp ../bots/order_book.cpp
BBO_OBJS = $(BBO_SRCS:.cpp=.o)


all: $(TARGET) $(PCAP_TARGET) $(BBO_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(PCAP_TARGET): $(PCAP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BBO_TARGET): $(BBO_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(PCAP_OBJS) $(PCAP_TARGET)

.PHONY: all clean