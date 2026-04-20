# Compiler and flags
CXX=g++
CXXFLAGS=-I.
#Targets 
all: Final_Project clean
Final_Project: Final_Project.o
$(CXX) -o Final_Project Final_Project.o
#This target creates Final_Project.o from Final_Project.cpp
Final_Project.o: Final_Project.cpp
$(CXX) -c Final_Project.cpp $(CXXFLAGS)
#The clean target is a conventional target
# for removing all files the makefile created.
.PHONY: clean
clean:
  rm -f *.o
