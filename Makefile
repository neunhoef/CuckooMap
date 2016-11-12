all:	ShardedCuckooMapTest CuckooMapTest InternalCuckooMapTest Makefile \
	CuckooMultiMapTest ShardedCuckooMultiMapTest PerformanceTest

#OPTIONS=-O0 -g -fsanitize=address -fsanitize=undefined
#OPTIONS=-O3
OPTIONS=-O0 -g

InternalCuckooMapTest:	InternalCuckooMapTest.cpp CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o InternalCuckooMapTest InternalCuckooMapTest.cpp -std=c++11 ${OPTIONS}

CuckooMapTest:	CuckooMapTest.cpp CuckooMap.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o CuckooMapTest CuckooMapTest.cpp -std=c++11 ${OPTIONS}

ShardedCuckooMapTest:	ShardedCuckooMapTest.cpp ShardedMap.h CuckooMap.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o ShardedCuckooMapTest ShardedCuckooMapTest.cpp -std=c++11 ${OPTIONS}

CuckooMultiMapTest:	CuckooMultiMapTest.cpp CuckooMultiMap.h CuckooMap.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o CuckooMultiMapTest CuckooMultiMapTest.cpp -std=c++11 ${OPTIONS}

ShardedCuckooMultiMapTest:	ShardedCuckooMultiMapTest.cpp CuckooMultiMap.h CuckooMap.h CuckooHelpers.h InternalCuckooMap.h ShardedMap.h Makefile
	g++ -Wall -o ShardedCuckooMultiMapTest ShardedCuckooMultiMapTest.cpp -std=c++11 ${OPTIONS}

PerformanceTest:	PerformanceTest.cpp CuckooMap.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o PerformanceTest PerformanceTest.cpp -std=c++11 ${OPTIONS} -DKEY_PAD=4 -DVALUE_PAD=4

test:	all
	./InternalCuckooMapTest
	./CuckooMapTest
	./ShardedCuckooMapTest
	./CuckooMultiMapTest
	./ShardedCuckooMultiMapTest

clean:
	rm -rf ShardedCuckooMapTest CuckooMultiMapTest CuckooMapTest \
		InternalCuckooMapTest ShardedCuckooMultiMapTest PerformanceTest
