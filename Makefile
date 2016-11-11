.PHONY: clean

CXXFLAGS += -Wall -I./include -std=c++11
#CXXFLAGS += -fsanitize=address -fsanitize=undefined

ifdef DEBUG
	CXXFLAGS += -O0 -g
else
	CXXFLAGS += -O3
endif

headers=$(wildcard include/cuckoomap/*h)
cpps=$(wildcard tests/*cpp)
tests=$(basename ${cpps})

all: ${tests}

test: all
	for f in ${tests}; do ./$$f; done;

clean:
	rm -fr tests/*o
	rm -fr ${tests}
