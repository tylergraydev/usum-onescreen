# Host tests for the pure parts of the plugin. Needs a PC C++17 compiler:
#   make -C tests            (MSYS2 / WSL / Linux g++)
#   make -C tests CXX=clang++
# MSVC: cl /std:c++17 /EHsc /I ..\Includes settings_test.cpp ..\Sources\Settings.cpp
CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I ../Includes
OUT      := build

TESTS    := $(OUT)/input_capture_test $(OUT)/settings_test

all: run

$(OUT)/input_capture_test: input_capture_test.cpp ../Sources/InputCapture.cpp ../Includes/InputCapture.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) input_capture_test.cpp ../Sources/InputCapture.cpp -o $@

$(OUT)/settings_test: settings_test.cpp ../Sources/Settings.cpp ../Includes/Settings.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) settings_test.cpp ../Sources/Settings.cpp -o $@

run: $(TESTS)
	./$(OUT)/input_capture_test
	./$(OUT)/settings_test

clean:
	rm -rf $(OUT)

.PHONY: all run clean
