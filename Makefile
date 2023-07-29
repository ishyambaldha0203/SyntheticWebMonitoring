# Makefile for the Synthetic Web Monitoring application

CXXFLAGS=-g -Wall -MMD -std=c++11

core_objects = Core.o
agent_objects = Agent.o

all : core agent

core: $(core_objects)
	g++ -o core $(core_objects)


agent: $(agent_objects)
	g++ -o agent $(agent_objects)


core: Core.cpp
agent: Agent.cpp


clean:
	rm -f *.o *.d core agent
