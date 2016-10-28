all:	CuckooCascadeTest CuckooCascadeTest1 Makefile

#OPTIONS=-O0 -g -fsanitize=address -fsanitize=undefined
#OPTIONS=-O3
OPTIONS=-O0 -g

CuckooCascadeTest1:	CuckooCascadeTest1.cpp CuckooCascade.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o CuckooCascadeTest1 CuckooCascadeTest1.cpp -std=c++11 ${OPTIONS}
	
CuckooCascadeTest:	CuckooCascadeTest.cpp CuckooCascade.h CuckooHelpers.h InternalCuckooMap.h Makefile
	g++ -Wall -o CuckooCascadeTest CuckooCascadeTest.cpp -std=c++11 ${OPTIONS}

