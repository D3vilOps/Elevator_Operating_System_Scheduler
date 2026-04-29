# Compiler and flags
CXX=g++
CXXFLAGS= -std=c++17 -wall -g -Wextra -pthread
LDFLAGS  := -pthread

# Targets
TARGET = Elevator_Scheduler
OBJS = Elevator_Scheduler.o

all: $(TARGET) clean

$(TARGET): $(OBJS)
  $(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

#This target creates Elevator_Scheduler.o from Elevator_Scheduler.cpp
Elevator_Scheduler.o: Elevator_Scheduler.cpp
  $(CXX) -c Elevator_Scheduler.cpp $(CXXFLAGS) $(LDFLAGS)

# For removing all object files the makefile created.
.PHONY: clean
clean:
  rm -f $(TARGET) $(OBJS)
