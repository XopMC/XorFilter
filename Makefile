# Author: Mikhail Khoroshavin aka "XopMC"

.PHONY: all clean

BIN_DIR := build
TARGET := $(BIN_DIR)/hex_to_xor

all: $(TARGET)

$(TARGET): Source.cpp xor_filter.h hex_key_utils.h intrin_local.h
	mkdir -p $(BIN_DIR)
	clang++ -O3 -std=c++20 -fopenmp Source.cpp -o $(TARGET) -ltbb

clean:
	rm -f $(TARGET)
