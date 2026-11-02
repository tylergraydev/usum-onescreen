# Host tests for the pure parts of the plugin. Needs a PC C++17 compiler:
#   make -C tests            (MSYS2 / WSL / Linux g++)
#   make -C tests CXX=clang++
CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I ../Includes
OUT      := build

all: run

$(OUT)/input_capture_test: input_capture_test.cpp ../Sources/InputCapture.cpp ../Includes/InputCapture.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) input_capture_test.cpp ../Sources/InputCapture.cpp -o $@

run: $(OUT)/input_capture_test
	./$(OUT)/input_capture_test

clean:
	rm -rf $(OUT)

.PHONY: all run clean
