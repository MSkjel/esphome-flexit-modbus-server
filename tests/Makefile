# The host tests, built with the sanitizers and run: make -C tests, or one of them, make -C tests camera_test.
# The ones with the component on the bus need MSkjel/ESP-ModbusRTUServer checked out, MODBUS=<its path>,
# ../modbus-library unless told. CI runs this too.
MODBUS ?= ../modbus-library
CXX    := g++
CXXFLAGS := -std=gnu++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -Istubs -I../packages/code
BUILD  := build
CODE   := ../packages/code
SERVER := ../esphome/components/flexit_modbus_server
CAMERA := ../esphome/components/flexit_panel_camera
SERVER_SRC := $(SERVER)/flexit_modbus_server.cpp $(MODBUS)/ModbusRTUServer.cpp $(CODE)/panel/pbus.c
HEADERS := $(wildcard *.h stubs/*.h stubs/*/*.h stubs/*/*/*.h $(CODE)/*.h $(CODE)/*/*.h $(SERVER)/*.h $(CAMERA)/*.h \
  $(MODBUS)/*.h)
TESTS  := update_test camera_test server_test restart_test humidity_boost_test summer_cooling_test

all: $(TESTS)
$(TESTS): %: $(BUILD)/%
	$<

$(BUILD)/update_test: update_test.cpp $(CODE)/panel/update.cpp $(CODE)/panel/pbus.c
$(BUILD)/camera_test: CXXFLAGS += -I$(CAMERA)
$(BUILD)/camera_test: camera_test.cpp $(CAMERA)/panel_camera.cpp
$(BUILD)/server_test $(BUILD)/restart_test $(BUILD)/humidity_boost_test $(BUILD)/summer_cooling_test: \
  CXXFLAGS += -I$(SERVER) -I$(MODBUS)
$(BUILD)/server_test: server_test.cpp $(SERVER_SRC)
$(BUILD)/restart_test: restart_test.cpp $(SERVER_SRC)
$(BUILD)/humidity_boost_test: humidity_boost_test.cpp $(SERVER_SRC)
$(BUILD)/summer_cooling_test: summer_cooling_test.cpp $(SERVER_SRC)
$(BUILD)/%: $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(filter %.cpp %.c,$^) -o $@

clean:
	rm -rf $(BUILD)
.PHONY: all clean $(TESTS)
