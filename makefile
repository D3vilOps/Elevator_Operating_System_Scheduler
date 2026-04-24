# Compiler and flags
CXX=g++
CXXFLAGS= -std=c++17 -wall -g -Wextra -pthread
LDFLAGS  := -pthread

# Targets
TARGET = Final_Project
OBJS = Final_Project.o

all: $(TARGET) clean

$(TARGET): $(OBJS)
  $(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

#This target creates Final_Project.o from Final_Project.cpp
Final_Project.o: Final_Project.cpp
  $(CXX) -c Final_Project.cpp $(CXXFLAGS) $(LDFLAGS)

# For removing all object files the makefile created.
.PHONY: clean
clean:
  rm -f $(TARGET) $(OBJS)
