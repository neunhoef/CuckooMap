CXXFLAGS += -O3 -Wall -I./include -std=c++11

headers=$(wildcard include/cuckoomap/*h)
cpps=$(wildcard tests/*cpp)
tests=$(cpps:tests/%.cpp=%)

VPATH := tests include/cuckoomap

%: %.cpp $(headers) Makefile
	$(CXX) $(CXXFLAGS) -o $@ $<

all: $(tests)

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
