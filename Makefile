CXX := clang++
CXXFLAGS := -O3 -ffast-math -Wall -Wextra -std=c++23

SRCS := bootimg.cpp main.cpp utils.cpp vendorbootimg.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := bootimg.h utils.h vendorbootimg.h

TARGET := mkbootimg

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -s -o $@ $^

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
