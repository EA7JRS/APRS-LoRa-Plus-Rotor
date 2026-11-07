# Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
# Pruebas del nucleo en el PC. ArduinoJson: ARDUINOJSON=/ruta/a/ArduinoJson/src (por defecto, la copia que baja PlatformIO)
ARDUINOJSON ?= ../.pio/libdeps/esp32dev/ArduinoJson/src
CXXFLAGS ?= -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined

test_core: test_core.cpp ../lib/RotorCore/src/*.h
	$(CXX) $(CXXFLAGS) -I../lib/RotorCore/src -I$(ARDUINOJSON) -DARDUINOJSON_ENABLE_STD_STRING=1 -o $@ test_core.cpp

run: test_core
	./test_core

clean:
	rm -f test_core
