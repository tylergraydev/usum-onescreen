# Host tests for the pure parts of the plugin. Needs a PC C++17 compiler:
#   make -C tests            (MSYS2 / WSL / Linux g++)
#   make -C tests CXX=clang++
# MSVC: cl /std:c++17 /EHsc /I ..\Includes settings_test.cpp ..\Sources\Settings.cpp
# sigscan_test.py needs Python 3.9+ (Windows: PYTHON="py -3"); pass
# CODE_BIN=<Ultra Sun v1.2 code.bin> to also run the real and shifted-copy checks.
CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -I ../Includes
OUT      := build
PYTHON   ?= python3
CODE_BIN ?=

TESTS    := $(OUT)/input_capture_test $(OUT)/settings_test $(OUT)/ui_host_test $(OUT)/party_data_test             $(OUT)/action_queue_test

all: run

$(OUT)/input_capture_test: input_capture_test.cpp ../Sources/InputCapture.cpp ../Includes/InputCapture.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) input_capture_test.cpp ../Sources/InputCapture.cpp -o $@

$(OUT)/settings_test: settings_test.cpp ../Sources/Settings.cpp ../Includes/Settings.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) settings_test.cpp ../Sources/Settings.cpp -o $@

$(OUT)/action_queue_test: action_queue_test.cpp ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) action_queue_test.cpp ../Sources/ActionQueue.cpp -o $@

$(OUT)/party_data_test: party_data_test.cpp ../Sources/PartyData.cpp ../Includes/PartyData.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) party_data_test.cpp ../Sources/PartyData.cpp -o $@

UI_SRCS  := $(addprefix ../Sources/UI/,Raster.cpp Text.cpp Icons.cpp Demo.cpp)
UI_HDRS  := $(wildcard ../Includes/UI/*.hpp)
PREVIEW  := ../analysis/private/ui-demo.png

$(OUT)/ui_host_test: ui_host_test.cpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) ui_host_test.cpp $(UI_SRCS) -o $@

run: $(TESTS)
	./$(OUT)/input_capture_test
	./$(OUT)/settings_test
	./$(OUT)/party_data_test
	./$(OUT)/action_queue_test
	$(PYTHON) -I ../tools/gen_layouts.py --check
	@mkdir -p $(dir $(PREVIEW))
	./$(OUT)/ui_host_test $(PREVIEW)
	$(PYTHON) -I sigscan_test.py $(CODE_BIN)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
