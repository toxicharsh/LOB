CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
LDFLAGS  =

SRCS   = order_book.cpp replay_engine.cpp main.cpp
OBJS   = $(SRCS:.cpp=.o)
TARGET = orderbook

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

# build with sanitizers for development
debug: CXXFLAGS = -std=c++17 -g -O0 -Wall -Wextra -fsanitize=address
debug: LDFLAGS  = -fsanitize=address
debug: clean $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

# header deps
order_book.o:     order_book.cpp order_book.h order.h
replay_engine.o:  replay_engine.cpp replay_engine.h order_book.h order.h
main.o:           main.cpp order_book.h replay_engine.h order.h
