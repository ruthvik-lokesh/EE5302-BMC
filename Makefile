CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

TARGET = bmc_main

SRCS = main.cpp bmc.cpp
HEADERS = fsm.h cnf.h bmc.h

$(TARGET): $(SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET) *.cnf

.PHONY: clean
