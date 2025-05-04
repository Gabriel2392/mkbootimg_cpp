CXX := clang++
CXXFLAGS := -O3 -ffast-math -Wall -Wextra -std=c++20

SRCS := bootimg.cpp main.cpp vendorbootimg.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := bootimg.h vendorbootimg.h

TARGET := mkbootimg

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -s -o $@ $^

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
