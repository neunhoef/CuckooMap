all:	CuckooMapTest CuckooMapTest1 Makefile

#OPTIONS=-O0 -g -fsanitize=address -fsanitize=undefined
#OPTIONS=-O3
OPTIONS=-O0 -g

CuckooMapTest1:	CuckooMapTest1.cpp CuckooMap.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o CuckooMapTest1 CuckooMapTest1.cpp -std=c++11 ${OPTIONS}
	
CuckooMapTest:	CuckooMapTest.cpp CuckooMap.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o CuckooMapTest CuckooMapTest.cpp -std=c++11 ${OPTIONS}

clean:
	rm -rf CuckooMapTest CuckooMapTest1
