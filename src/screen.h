// Double-buffered drawing for the 128x160 TFT.
//
// Everything is drawn into an off-screen canvas first and then pushed to the
// panel in one go. Keeping the previous screen ("front") and the next one
// ("back") in RAM is what makes transitions possible; it also means text
// scrolls without flicker.
//
// The panel itself always stays in its native portrait orientation; each
// canvas carries its own rotation (0 = portrait, 1 = landscape), so pushing
// is always a straight copy.
#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "settings.h"

constexpr int SCREEN_W = 128, SCREEN_H = 160;  // native panel size

extern Adafruit_ST7735 tft;

namespace screen {

bool begin();                  // allocates both canvases; false = out of memory
GFXcanvas16 &front();          // what's currently on the panel
GFXcanvas16 &back();           // draw the next screen here

void push(GFXcanvas16 &c);     // copy a canvas to the panel
void present();                // push back(), then swap so it becomes front()

// Animate from front() to back(), then swap. Calls `keepGoing` between
// animation steps; if it returns false the transition is abandoned (used for
// card removal) and the function returns false.
bool transitionTo(Transition t, float seconds, bool (*keepGoing)());

}  // namespace screen
