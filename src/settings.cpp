#include "settings.h"

namespace {

struct Named { const char *name; Transition t; };
const Named TRANSITIONS[] = {
  {"none", Transition::None},           {"fade", Transition::Fade},
  {"crossfade", Transition::Crossfade}, {"wipe_left", Transition::WipeLeft},
  {"wipe_right", Transition::WipeRight}, {"wipe_up", Transition::WipeUp},
  {"wipe_down", Transition::WipeDown},  {"slide_left", Transition::SlideLeft},
  {"slide_right", Transition::SlideRight}, {"slide_up", Transition::SlideUp},
  {"slide_down", Transition::SlideDown}, {"dissolve", Transition::Dissolve},
  {"blinds", Transition::Blinds},       {"random", Transition::Random},
};

struct NamedColor { const char *name; uint32_t rgb; };
const NamedColor COLORS[] = {
  {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000},
  {"green", 0x00FF00}, {"blue", 0x0000FF}, {"yellow", 0xFFFF00},
  {"cyan", 0x00FFFF},  {"magenta", 0xFF00FF}, {"orange", 0xFF8000},
  {"gray", 0x808080},  {"grey", 0x808080},
};

uint16_t rgb565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

using Warnings = std::vector<String>;

void warn(Warnings &w, const String &path, const String &msg) {
  w.push_back(path + ": " + msg);
}

// Each reader leaves `out` unchanged if the key is absent or invalid.
void readNumber(JsonObjectConst o, const char *key, float &out, float lo, float hi,
                const String &path, Warnings &w) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<float>()) { warn(w, path + key, "must be a number"); return; }
  float f = v.as<float>();
  if (f < lo || f > hi) {
    warn(w, path + key, "out of range " + String(lo, 1) + "-" + String(hi, 1));
    f = constrain(f, lo, hi);
  }
  out = f;
}

template <typename T>
void readInt(JsonObjectConst o, const char *key, T &out, long lo, long hi,
             const String &path, Warnings &w) {
  float f = out;
  readNumber(o, key, f, lo, hi, path, w);
  out = (T)lroundf(f);
}

void readBool(JsonObjectConst o, const char *key, bool &out, const String &path, Warnings &w) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<bool>()) { warn(w, path + key, "must be true or false"); return; }
  out = v.as<bool>();
}

void readColor(JsonObjectConst o, const char *key, uint16_t &out, const String &path, Warnings &w) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  if (!v.is<const char *>()) { warn(w, path + key, "must be a string like \"#FF8800\""); return; }
  String s = v.as<const char *>();
  s.trim();
  s.toLowerCase();
  for (auto &c : COLORS)
    if (s == c.name) { out = rgb565(c.rgb); return; }
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.length() == 3)  // #RGB shorthand
    s = String(s[0]) + s[0] + s[1] + s[1] + s[2] + s[2];
  char *end;
  uint32_t rgb = strtoul(s.c_str(), &end, 16);
  if (s.length() != 6 || *end) { warn(w, path + key, "unknown colour \"" + s + "\""); return; }
  out = rgb565(rgb);
}

void readTransition(JsonObjectConst o, const char *key, Transition &out,
                    const String &path, Warnings &w) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return;
  String s = v.as<const char *>() ? v.as<const char *>() : "";
  s.toLowerCase();
  for (auto &t : TRANSITIONS)
    if (s == t.name) { out = t.t; return; }
  warn(w, path + key, "unknown transition \"" + s + "\"");
}

// Reads a string key that must be one of `names`; returns its index or -1.
int readChoice(JsonObjectConst o, const char *key, std::initializer_list<const char *> names,
               const String &path, Warnings &w) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return -1;
  String s = v.as<const char *>() ? v.as<const char *>() : "";
  s.toLowerCase();
  int i = 0;
  String all;
  for (const char *n : names) {
    if (s == n) return i;
    all += (i ? ", " : "") + String(n);
    i++;
  }
  warn(w, path + key, "must be one of: " + all);
  return -1;
}

void checkKeys(JsonObjectConst o, std::initializer_list<const char *> known,
               const String &path, Warnings &w) {
  for (JsonPairConst kv : o) {
    const char *k = kv.key().c_str();
    if (k[0] == '_') continue;  // "_comment" etc. are allowed
    bool ok = false;
    for (const char *n : known) if (!strcmp(k, n)) { ok = true; break; }
    if (!ok) warn(w, path + k, "unknown setting (ignored)");
  }
}

void applyText(JsonObjectConst o, TextSettings &t, Warnings &w) {
  const String p = "text.";
  checkKeys(o, {"mode", "page_seconds", "scroll_speed", "chars_per_second", "hold_seconds",
                "size", "color", "background", "align", "valign", "margin", "line_spacing",
                "orientation", "page_transition", "page_transition_seconds"}, p, w);
  int i;
  if ((i = readChoice(o, "mode", {"page", "scroll", "typewriter"}, p, w)) >= 0) t.mode = (TextMode)i;
  readNumber(o, "page_seconds", t.page_seconds, 0.1, 3600, p, w);
  readNumber(o, "scroll_speed", t.scroll_speed, 1, 500, p, w);
  readNumber(o, "chars_per_second", t.chars_per_second, 1, 1000, p, w);
  readNumber(o, "hold_seconds", t.hold_seconds, 0, 3600, p, w);
  readInt(o, "size", t.size, 1, 4, p, w);
  readColor(o, "color", t.color, p, w);
  readColor(o, "background", t.background, p, w);
  if ((i = readChoice(o, "align", {"left", "center", "right"}, p, w)) >= 0) t.align = (Align)i;
  if ((i = readChoice(o, "valign", {"top", "middle", "bottom"}, p, w)) >= 0) t.valign = (VAlign)i;
  readInt(o, "margin", t.margin, 0, 40, p, w);
  readInt(o, "line_spacing", t.line_spacing, -4, 40, p, w);
  if ((i = readChoice(o, "orientation", {"portrait", "landscape"}, p, w)) >= 0) t.landscape = i == 1;
  readTransition(o, "page_transition", t.page_transition, p, w);
  readNumber(o, "page_transition_seconds", t.page_transition_seconds, 0, 10, p, w);
}

void applyImage(JsonObjectConst o, ImageSettings &s, Warnings &w) {
  const String p = "image.";
  checkKeys(o, {"duration_seconds", "background"}, p, w);
  readNumber(o, "duration_seconds", s.duration_seconds, 0.1, 3600, p, w);
  readColor(o, "background", s.background, p, w);
}

void applyVideo(JsonObjectConst o, VideoSettings &s, Warnings &w) {
  const String p = "video.";
  checkKeys(o, {"speed", "loops"}, p, w);
  readNumber(o, "speed", s.speed, 0.1, 4, p, w);
  readInt(o, "loops", s.loops, 1, 1000, p, w);
}

}  // namespace

void applySettings(JsonObjectConst o, Settings &s, Warnings &w) {
  readTransition(o, "transition", s.transition, "", w);
  readNumber(o, "transition_seconds", s.transition_seconds, 0, 10, "", w);
  // Returns the sub-object `key`, or a null object if absent/invalid.
  auto section = [&](const char *key) -> JsonObjectConst {
    JsonVariantConst v = o[key];
    if (!v.isNull() && !v.is<JsonObjectConst>()) warn(w, key, "must be an object { ... }");
    return v.as<JsonObjectConst>();
  };
  if (JsonObjectConst x = section("text")) applyText(x, s.text, w);
  if (JsonObjectConst x = section("image")) applyImage(x, s.image, w);
  if (JsonObjectConst x = section("video")) applyVideo(x, s.video, w);
}

void applyGlobal(JsonObjectConst o, GlobalSettings &g, Warnings &w) {
  checkKeys(o, {"backlight", "shuffle", "loop", "error_seconds", "transition",
                "transition_seconds", "text", "image", "video", "playlist"}, "", w);
  readInt(o, "backlight", g.backlight, 0, 255, "", w);
  readBool(o, "shuffle", g.shuffle, "", w);
  readBool(o, "loop", g.loop, "", w);
  readNumber(o, "error_seconds", g.error_seconds, 0, 60, "", w);
}

const char *transitionName(Transition t) {
  for (auto &n : TRANSITIONS)
    if (n.t == t) return n.name;
  return "?";
}
