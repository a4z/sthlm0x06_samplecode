
CFLAGS= -Wall -Wextra -g -pedantic
CXXFLAGS= -Wall -Wextra -g -pedantic -std=c++11
LDFLAGS= -lsqlite3
#LDFLAGS= -z nodeflib

all: sample1

sample1: sample1.o
	g++ $(LDFLAGS) $< -o $@

%.o : %.cpp
	g++ $(CXXFLAGS) -c $<


clean:
	rm -f *.o sample1

