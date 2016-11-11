CXXFLAGS += -O3 -Wall -I./include -std=c++11

headers=$(wildcard include/cuckoomap/*h)
cpps=$(wildcard tests/*cpp)
tests=$(basename $(cpps))

all: $(tests) $(headers)

debug: all
debug: CXXFLAGS += -O0 -g

sanatize: debug
sanatize: CXXFLAGS += -fsanitize=address -fsanitize=undefined

test: all
	for f in $(tests); do ./$$f; done;

clean:
	$(RM) -fr tests/*o
	$(RM) -fr ${tests}
.PHONY: clean
