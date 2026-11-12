# Copyright (C) 2026 Manuel - EA7JRS - GPL-3.0-or-later
# Pruebas en el PC. ArduinoJson: ARDUINOJSON=/ruta/a/ArduinoJson/src (por defecto, la copia que baja PlatformIO)
#   make run      nucleo + pantalla tactil (las pantallas quedan en lcd_out/*.ppm)
ARDUINOJSON ?= ../.pio/libdeps/esp32dev/ArduinoJson/src
CXXFLAGS ?= -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined

all: test_core lcd_preview

# El nucleo usa formulas del nucleo del plotter (lib/plotter_core: astro, wind, wmm)
PCORE = ../lib/plotter_core/src
test_core: test_core.cpp ../lib/RotorCore/src/*.h $(PCORE)/*.h $(PCORE)/*.cpp
	$(CXX) $(CXXFLAGS) -I../lib/RotorCore/src -I$(PCORE) -I$(ARDUINOJSON) -DARDUINOJSON_ENABLE_STD_STRING=1 -o $@ test_core.cpp $(PCORE)/astro.cpp $(PCORE)/geo.cpp $(PCORE)/wind.cpp $(PCORE)/wmm.cpp

lcd_preview: lcd_preview.cpp ../src/lcd_ui.h ../src/lcd_gfx.h ../src/lcd_fonts.h ../src/lcd_logo.h ../src/lcd_splash.h
	$(CXX) $(CXXFLAGS) -o $@ lcd_preview.cpp

run: test_core lcd_preview
	./test_core
	mkdir -p lcd_out && ./lcd_preview lcd_out

clean:
	rm -rf test_core lcd_preview lcd_out
