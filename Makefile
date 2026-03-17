CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pthread
TARGETS  := server client monitor

.PHONY: all clean

all: $(TARGETS)

server: server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

client: client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

monitor: monitor.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS) chat_monitor.log
