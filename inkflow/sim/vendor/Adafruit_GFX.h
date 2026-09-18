// ABOUTME: Shim so the stock Adafruit font headers compile on a desktop unchanged.
// ABOUTME: They include this for the GFXfont types and nothing else.

#pragma once
#include <cstdint>
#include "gfxfont.h"

// The font data is declared PROGMEM on AVR. Off-device it is ordinary memory.
#ifndef PROGMEM
#define PROGMEM
#endif
