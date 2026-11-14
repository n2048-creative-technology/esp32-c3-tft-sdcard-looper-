// Playback settings, loaded at run time from /config.json on the SD card.
// Every field has a default, so the config file (and every key in it) is
// optional. See README.md for the file format.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

enum class Transition : uint8_t {
  None, Fade, Crossfade,
  WipeLeft, WipeRight, WipeUp, WipeDown,
  SlideLeft, SlideRight, SlideUp, SlideDown,
  Dissolve, Blinds,
  Random,
};

enum class TextMode : uint8_t { Page, Scroll, Typewriter };
enum class Align : uint8_t { Left, Center, Right };
enum class VAlign : uint8_t { Top, Middle, Bottom };

struct TextSettings {
  TextMode mode = TextMode::Page;
  float page_seconds = 5;             // time each page stays (page/typewriter)
  float scroll_speed = 20;            // pixels per second (scroll)
  float chars_per_second = 30;        // typing speed (typewriter)
  float hold_seconds = 1.5;           // pause before/after scrolling
  uint8_t size = 1;                   // font scale 1-4 (1 = 6x8 px characters)
  uint16_t color = 0xFFFF;            // RGB565
  uint16_t background = 0x0000;
  Align align = Align::Left;
  VAlign valign = VAlign::Top;
  uint8_t margin = 0;                 // pixels on every side
  int8_t line_spacing = 0;            // extra pixels between lines
  bool landscape = false;
  Transition page_transition = Transition::None;
  float page_transition_seconds = 0.3;
};

struct ImageSettings {
  float duration_seconds = 5;
  uint16_t background = 0x0000;       // around images smaller than the screen
};

struct VideoSettings {
  float speed = 1.0;                  // playback speed multiplier
  uint16_t loops = 1;                 // times to play before moving on
};

// Settings that can be set globally and overridden per playlist entry.
struct Settings {
  Transition transition = Transition::None;
  float transition_seconds = 0.5;
  TextSettings text;
  ImageSettings image;
  VideoSettings video;
};

// Settings that only make sense for the whole card.
struct GlobalSettings {
  uint8_t backlight = 200;            // 0-255
  bool shuffle = false;
  bool loop = true;                   // false = stop on the last item
  float error_seconds = 2;
};

// Applies every recognised key in `obj` on top of `s`. Problems (bad values,
// unknown keys) are appended to `warnings`.
void applySettings(JsonObjectConst obj, Settings &s, std::vector<String> &warnings);
void applyGlobal(JsonObjectConst obj, GlobalSettings &g, std::vector<String> &warnings);

const char *transitionName(Transition t);
