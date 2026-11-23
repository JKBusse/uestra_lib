# Host-Tests: make test     (lädt ArduinoJson einmalig nach .deps/)
ARDUINOJSON_VERSION ?= 7.4.2
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1

test: test_core
	./test_core

test_core: test_core.cpp ../../src/UestraCore.h .deps/ArduinoJson.h
	$(CXX) $(CXXFLAGS) -I../../src -I.deps test_core.cpp -o test_core

.deps/ArduinoJson.h:
	mkdir -p .deps
	curl -fsSL https://github.com/bblanchon/ArduinoJson/releases/download/v$(ARDUINOJSON_VERSION)/ArduinoJson-v$(ARDUINOJSON_VERSION).h -o $@

clean:
	rm -f test_core

# Übersetzt den ESP32-Client gegen Attrappen des Arduino-Cores (nur Syntax-/Typprüfung,
# kein Ersatz für einen echten Build mit PlatformIO/Arduino-IDE).
check-client: .deps/ArduinoJson.h
	$(CXX) $(CXXFLAGS) -DARDUINO=10819 -DARDUINOJSON_ENABLE_PROGMEM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -I stubs -I../../src -I.deps -fsyntax-only ../../src/UestraDepartures.cpp
	@echo "Client-Syntaxcheck ok"
