# Author: Mikhail Khoroshavin aka "XopMC"

.PHONY: all

all:
	clang++ -O3 -std=c++20 -fopenmp Source.cpp -o hex_to_xor -ltbb
