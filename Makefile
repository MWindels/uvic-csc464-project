ref_tester_make: src/tst/ref_tester.cpp
	g++ -Wall -std=c++17 -Isrc src/tst/ref_tester.cpp -pthread -latomic -march=native -o ref_tester