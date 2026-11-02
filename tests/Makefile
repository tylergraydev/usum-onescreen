# Host tests for the pure parts of the plugin. Needs a PC C++17 compiler:
#   make -C tests            (MSYS2 / WSL / Linux g++)
#   make -C tests CXX=clang++
# MSVC: cl /std:c++17 /EHsc /I ..\Includes settings_test.cpp ..\Sources\Settings.cpp
CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I ../Includes
OUT      := build

TESTS    := $(OUT)/input_capture_test $(OUT)/settings_test $(OUT)/ui_host_test

all: run

$(OUT)/input_capture_test: input_capture_test.cpp ../Sources/InputCapture.cpp ../Includes/InputCapture.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) input_capture_test.cpp ../Sources/InputCapture.cpp -o $@

$(OUT)/settings_test: settings_test.cpp ../Sources/Settings.cpp ../Includes/Settings.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) settings_test.cpp ../Sources/Settings.cpp -o $@

UI_SRCS  := $(addprefix ../Sources/UI/,Raster.cpp Text.cpp Icons.cpp Demo.cpp)
UI_HDRS  := $(wildcard ../Includes/UI/*.hpp)
PREVIEW  := ../analysis/private/ui-demo.png

$(OUT)/ui_host_test: ui_host_test.cpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) ui_host_test.cpp $(UI_SRCS) -o $@

run: $(TESTS)
	./$(OUT)/input_capture_test
	./$(OUT)/settings_test
	@mkdir -p $(dir $(PREVIEW))
	./$(OUT)/ui_host_test $(PREVIEW)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
