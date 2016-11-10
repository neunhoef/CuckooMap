all: ShardedCuckooMapTest CuckooMapTest InternalCuckooMapTest Makefile \
	CuckooMultiMapTest ShardedCuckooMultiMapTest

#OPTIONS=-O0 -g -fsanitize=address -fsanitize=undefined
#OPTIONS=-O3
OPTIONS=-O0 -g
CPPFLAGS= -I./include -std=c++11

InternalCuckooMapTest: tests/InternalCuckooMapTest.cpp include/cuckoomap/CuckooHelpers.h include/cuckoomap/InternalCuckooMap.h Makefile
	$(CXX) -Wall -o InternalCuckooMapTest tests/InternalCuckooMapTest.cpp $(CPPFLAGS) ${OPTIONS}

CuckooMapTest: tests/CuckooMapTest.cpp include/cuckoomap/CuckooMap.h include/cuckoomap/CuckooHelpers.h include/cuckoomap/InternalCuckooMap.h Makefile
	$(CXX) -Wall -o CuckooMapTest tests/CuckooMapTest.cpp $(CPPFLAGS) ${OPTIONS}

ShardedCuckooMapTest: tests/ShardedCuckooMapTest.cpp include/cuckoomap/ShardedMap.h include/cuckoomap/CuckooMap.h include/cuckoomap/CuckooHelpers.h include/cuckoomap/InternalCuckooMap.h Makefile
	$(CXX) -Wall -o ShardedCuckooMapTest tests/ShardedCuckooMapTest.cpp $(CPPFLAGS) ${OPTIONS}

CuckooMultiMapTest: tests/CuckooMultiMapTest.cpp include/cuckoomap/CuckooMultiMap.h include/cuckoomap/CuckooMap.h include/cuckoomap/CuckooHelpers.h include/cuckoomap/InternalCuckooMap.h Makefile
	$(CXX) -Wall -o CuckooMultiMapTest tests/CuckooMultiMapTest.cpp $(CPPFLAGS) ${OPTIONS}

ShardedCuckooMultiMapTest: tests/ShardedCuckooMultiMapTest.cpp include/cuckoomap/CuckooMultiMap.h include/cuckoomap/CuckooMap.h include/cuckoomap/CuckooHelpers.h include/cuckoomap/InternalCuckooMap.h include/cuckoomap/ShardedMap.h Makefile
	$(CXX) -Wall -o ShardedCuckooMultiMapTest tests/ShardedCuckooMultiMapTest.cpp $(CPPFLAGS) ${OPTIONS}

test: all
	./InternalCuckooMapTest
	./CuckooMapTest
	./ShardedCuckooMapTest
	./CuckooMultiMapTest
	./ShardedCuckooMultiMapTest

clean:
	rm -rf ShardedCuckooMapTest CuckooMultiMapTest CuckooMapTest \
		InternalCuckooMapTest ShardedCuckooMultiMapTest
