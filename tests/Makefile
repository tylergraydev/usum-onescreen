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

TESTS    := $(OUT)/input_capture_test $(OUT)/settings_test $(OUT)/ui_host_test $(OUT)/party_data_test $(OUT)/action_queue_test $(OUT)/xmenu_test $(OUT)/battle_menu_test $(OUT)/keyboard_test $(OUT)/pokedex_test $(OUT)/native_screens_test $(OUT)/bag_test $(OUT)/map_test $(OUT)/party_menu_test $(OUT)/boxes_test $(OUT)/field_settle_test $(OUT)/move_learn_test

all: run

$(OUT)/input_capture_test: input_capture_test.cpp ../Sources/InputCapture.cpp ../Includes/InputCapture.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) input_capture_test.cpp ../Sources/InputCapture.cpp -o $@

$(OUT)/field_settle_test: field_settle_test.cpp ../Includes/FieldSettle.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) field_settle_test.cpp -o $@

$(OUT)/settings_test: settings_test.cpp ../Sources/Settings.cpp ../Includes/Settings.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) settings_test.cpp ../Sources/Settings.cpp -o $@

$(OUT)/action_queue_test: action_queue_test.cpp ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) action_queue_test.cpp ../Sources/ActionQueue.cpp -o $@

$(OUT)/party_data_test: party_data_test.cpp ../Sources/PartyData.cpp ../Includes/PartyData.hpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) party_data_test.cpp ../Sources/PartyData.cpp -o $@

UI_SRCS  := $(addprefix ../Sources/UI/,Raster.cpp Text.cpp Font.cpp Layer.cpp Icons.cpp Demo.cpp)
UI_HDRS  := $(wildcard ../Includes/UI/*.hpp)
PREVIEW  := ../analysis/private/ui-demo.png
XMENU_PREVIEW := ../analysis/private/xmenu-preview.ppm
KEYBOARD_PREVIEW := ../analysis/private/keyboard-preview.ppm
DEX_PREVIEW := ../analysis/private/pokedex-preview
BAG_PREVIEW := ../analysis/private/bag-preview.ppm

$(OUT)/xmenu_test: xmenu_test.cpp ../Sources/Screens/XMenuGrid.cpp ../Includes/Screens/XMenuGrid.hpp                    ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) xmenu_test.cpp ../Sources/Screens/XMenuGrid.cpp ../Sources/ActionQueue.cpp $(UI_SRCS) -o $@

$(OUT)/keyboard_test: keyboard_test.cpp ../Sources/Screens/KeyboardGrid.cpp ../Includes/Screens/KeyboardGrid.hpp                     ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) keyboard_test.cpp ../Sources/Screens/KeyboardGrid.cpp ../Sources/ActionQueue.cpp $(UI_SRCS) -o $@

$(OUT)/pokedex_test: pokedex_test.cpp ../Sources/Screens/PokedexList.cpp ../Includes/Screens/PokedexList.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) pokedex_test.cpp ../Sources/Screens/PokedexList.cpp $(UI_SRCS) -o $@

SCREEN_SRCS := $(addprefix ../Sources/Screens/,NativeScreen.cpp Options.cpp TrainerPassport.cpp BattleVideo.cpp)
SCREEN_HDRS := $(addprefix ../Includes/Screens/,NativeScreen.hpp Options.hpp TrainerPassport.hpp BattleVideo.hpp)
SCREEN_PREVIEWS := ../analysis/private

$(OUT)/native_screens_test: native_screens_test.cpp $(SCREEN_SRCS) $(SCREEN_HDRS)                     ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) native_screens_test.cpp $(SCREEN_SRCS) ../Sources/ActionQueue.cpp $(UI_SRCS) -o $@

$(OUT)/bag_test: bag_test.cpp ../Sources/Screens/BagList.cpp ../Includes/Screens/BagList.hpp                   ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) bag_test.cpp ../Sources/Screens/BagList.cpp ../Sources/ActionQueue.cpp $(UI_SRCS) -o $@
MAP_PREVIEW := ../analysis/private/map-preview.ppm
PLACE_GARC  ?= ../analysis/private/romfs-base/a/0/3/2

# PLACE_GARC: the user's own English text GARC (a/0/3/2), for the real
# place-name check; skipped if missing (CI).
MAP_SRCS := ../Sources/Screens/MapView.cpp ../Sources/RomfsText.cpp ../Sources/PartyData.cpp

$(OUT)/map_test: map_test.cpp $(MAP_SRCS) ../Includes/Screens/MapView.hpp ../Includes/Where.hpp ../Includes/RomfsText.hpp ../Includes/PartyData.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) map_test.cpp $(MAP_SRCS) $(UI_SRCS) -o $@

PARTY_PREVIEW := ../analysis/private/party-preview.ppm ../analysis/private/summary-preview.ppm

$(OUT)/party_menu_test: party_menu_test.cpp ../Sources/Screens/PartyMenu.cpp ../Includes/Screens/PartyMenu.hpp                         ../Sources/ActionQueue.cpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) party_menu_test.cpp ../Sources/Screens/PartyMenu.cpp ../Sources/ActionQueue.cpp $(UI_SRCS) -o $@

BOXES_PREVIEW := ../analysis/private/boxes-preview.ppm

BOXES_SRCS := ../Sources/BoxData.cpp ../Sources/PartyData.cpp ../Sources/Screens/BoxesGrid.cpp ../Sources/ActionQueue.cpp
BOXES_HDRS := ../Includes/BoxData.hpp ../Includes/PartyData.hpp ../Includes/Screens/BoxesGrid.hpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp

$(OUT)/boxes_test: boxes_test.cpp $(BOXES_SRCS) $(BOXES_HDRS) $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) boxes_test.cpp $(BOXES_SRCS) $(UI_SRCS) -o $@

LEARN_PREVIEW := ../analysis/private/move-learn-preview.ppm
MOVE_GARC     ?= ../analysis/private/romfs-base/a/0/1/1

# MOVE_GARC: the user's own move data GARC (a/0/1/1), for the real move
# check; skipped if missing (CI).
LEARN_SRCS := ../Sources/Screens/MoveLearnMenu.cpp ../Sources/Screens/BattleMenu.cpp ../Sources/MoveData.cpp ../Sources/RomfsText.cpp ../Sources/PartyData.cpp ../Sources/ActionQueue.cpp
LEARN_HDRS := ../Includes/Screens/MoveLearnMenu.hpp ../Includes/Screens/BattleMenu.hpp ../Includes/MoveData.hpp ../Includes/RomfsText.hpp ../Includes/ActionQueue.hpp ../Includes/Layouts.hpp

$(OUT)/move_learn_test: move_learn_test.cpp $(LEARN_SRCS) $(LEARN_HDRS) $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) move_learn_test.cpp $(LEARN_SRCS) $(UI_SRCS) -o $@

$(OUT)/ui_host_test: ui_host_test.cpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) ui_host_test.cpp $(UI_SRCS) -o $@

$(OUT)/battle_menu_test: battle_menu_test.cpp ../Sources/Screens/BattleMenu.cpp ../Includes/Screens/BattleMenu.hpp $(UI_SRCS) $(UI_HDRS)
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) battle_menu_test.cpp ../Sources/Screens/BattleMenu.cpp $(UI_SRCS) -o $@

run: $(TESTS)
	./$(OUT)/input_capture_test
	./$(OUT)/settings_test
	./$(OUT)/field_settle_test
	./$(OUT)/party_data_test
	./$(OUT)/action_queue_test
	./$(OUT)/battle_menu_test
	$(PYTHON) -I ../tools/gen_layouts.py --check
	@mkdir -p $(dir $(PREVIEW))
	./$(OUT)/ui_host_test $(PREVIEW)
	./$(OUT)/xmenu_test $(XMENU_PREVIEW)
	./$(OUT)/keyboard_test $(KEYBOARD_PREVIEW)
	./$(OUT)/pokedex_test $(DEX_PREVIEW)
	./$(OUT)/native_screens_test $(SCREEN_PREVIEWS)
	./$(OUT)/bag_test $(BAG_PREVIEW)
	./$(OUT)/map_test $(MAP_PREVIEW) $(PLACE_GARC)
	./$(OUT)/party_menu_test $(PARTY_PREVIEW)
	./$(OUT)/boxes_test $(BOXES_PREVIEW)
	./$(OUT)/move_learn_test $(LEARN_PREVIEW) $(MOVE_GARC)
	$(PYTHON) -I sigscan_test.py $(CODE_BIN)

clean:
	rm -rf $(OUT)

.PHONY: all run clean
