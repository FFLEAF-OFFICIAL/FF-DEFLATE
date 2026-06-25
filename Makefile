ifeq ($(origin CXX),default)
CXX := C:/MinGW/bin/g++.exe
endif

ifeq ($(origin AR),default)
AR := C:/MinGW/bin/ar.exe
endif
CXXFLAGS := -std=c++11 -O3 -DNDEBUG -Wall -Wextra -pedantic -Iinclude
BUILD := build

LIB := $(BUILD)/libff-deflate.a
TEST := $(BUILD)/test_ff_deflate.exe
EXAMPLE := $(BUILD)/ff_deflate_basic.exe

.PHONY: all test example clean dirs

all: $(LIB) $(TEST) $(EXAMPLE)

dirs:
	cmd /C if not exist "$(BUILD)" mkdir "$(BUILD)"

$(BUILD)/ff_deflate.o: src/ff_deflate.cpp include/ff_deflate.h | dirs
	$(CXX) $(CXXFLAGS) -c src/ff_deflate.cpp -o $@

$(LIB): $(BUILD)/ff_deflate.o
	$(AR) rcs $@ $<

$(BUILD)/test_ff_deflate.o: tests/test_ff_deflate.cpp include/ff_deflate.h | dirs
	$(CXX) $(CXXFLAGS) -c tests/test_ff_deflate.cpp -o $@

$(TEST): $(BUILD)/test_ff_deflate.o $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/basic.o: examples/basic.cpp include/ff_deflate.h | dirs
	$(CXX) $(CXXFLAGS) -c examples/basic.cpp -o $@

$(EXAMPLE): $(BUILD)/basic.o $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: all
	cd $(BUILD) && test_ff_deflate.exe

example: all
	cd $(BUILD) && ff_deflate_basic.exe

clean:
	cmd /C if exist "$(BUILD)" rmdir /S /Q "$(BUILD)"
