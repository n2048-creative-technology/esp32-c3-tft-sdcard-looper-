// XIAO ESP32-C3 + 128x160 ST7735 SPI TFT with onboard SD card reader.
//
// Plays the files on the SD card as a looping playlist:
//   .txt          text: paged, smoothly scrolled, or typed out
//   .bmp          24/32-bit uncompressed bitmap
//   .jpg / .jpeg  baseline JPEG
//   .avi          MJPEG video (no audio)
//
// How things play (timing, transitions, text style, order, ...) comes from an
// optional /config.json on the card, read every time a card is inserted, so
// nothing needs re-flashing. Without it, every supported file in the root
// plays in alphabetical order with default settings. See README.md.
//
// The TFT and SD card share one SPI bus. Wiring is documented in README.md.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <JPEGDEC.h>
#include "settings.h"
#include "screen.h"

// ---- Pin map (XIAO label -> GPIO) ----------------------------------------
constexpr int PIN_SCK      = 8;   // D8  -> TFT SCK  + SD_SCK
constexpr int PIN_MOSI     = 10;  // D10 -> TFT SDA  + SD_MOSI
constexpr int PIN_MISO     = 9;   // D9  -> SD_MISO (TFT doesn't use MISO)
constexpr int PIN_TFT_CS   = 3;   // D1  -> CS
constexpr int PIN_TFT_DC   = 4;   // D2  -> A0 (data/command)
constexpr int PIN_TFT_RST  = 5;   // D3  -> RESET
constexpr int PIN_TFT_LED  = 6;   // D4  -> LED (backlight, PWM)
constexpr int PIN_SD_CS    = 7;   // D5  -> SD_CS

// SPI clocks. Lower these if you see corrupted pixels or SD read errors
// (long wires, breadboard).
constexpr uint32_t TFT_SPI_HZ = 27000000;
constexpr uint32_t SD_SPI_HZ  = 20000000;

constexpr uint32_t CARD_POLL_MS    = 1000;   // how often to check for card removal
constexpr size_t   MAX_TEXT_BYTES  = 16384;  // cap on how much of a text file we load
constexpr size_t   MAX_FRAME_BYTES = 49152;  // largest single video frame (JPEG)
constexpr size_t   MAX_CONFIG_BYTES = 16384;
const char *CONFIG_PATH = "/config.json";

constexpr int FONT_W = 6, FONT_H = 8;  // built-in GFX font at size 1

Adafruit_ST7735 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
JPEGDEC jpeg;
uint8_t *frameBuf = nullptr;

// ---- Playlist -----------------------------------------------------------------
enum class Kind { Text, Bmp, Jpeg, Video };
struct Item {
  String path;
  Kind kind;
  Settings s;
};

enum class Play { Done, CardGone };

bool mounted = false;
GlobalSettings global;
std::vector<Item> playlist;
size_t current = 0;
bool playedOnce = false;  // has the current single-item playlist shown already?
uint32_t lastPollMs = 0;

// ---- Card presence / waiting ------------------------------------------------
// Reads sector 0 straight from the card. Filesystem calls can be answered from
// cache after the card is pulled, so a raw read is the reliable presence test.
static bool cardPresent() {
  static uint8_t sector[512];
  return SD.readRAW(sector, 0);
}

// Polls for card removal at most every CARD_POLL_MS. False = card gone.
static bool pollCard() {
  uint32_t now = millis();
  if (now - lastPollMs < CARD_POLL_MS) return true;
  lastPollMs = now;
  return cardPresent();
}

static bool waitMs(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    if (!pollCard()) return false;
    delay(5);
  }
  return true;
}
static bool waitSeconds(float s) { return waitMs(s * 1000); }

static Play holdUntilRemoved() {
  while (waitMs(CARD_POLL_MS)) {}
  return Play::CardGone;
}

// ---- Text layout ------------------------------------------------------------
// Word-wrap `text` to `width` characters per line.
static std::vector<String> wrapText(const String &text, int width) {
  std::vector<String> lines;
  String line, word;

  auto flushWord = [&]() {
    while (word.length() > 0) {
      int space = line.length() ? 1 : 0;
      if ((int)(line.length() + space + word.length()) <= width) {
        if (space) line += ' ';
        line += word;
        word = "";
      } else if (line.length() == 0) {
        line = word.substring(0, width);  // too long for a line: hard-break
        word = word.substring(width);
        lines.push_back(line);
        line = "";
      } else {
        lines.push_back(line);
        line = "";
      }
    }
  };

  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '\r') continue;
    if (c == '\n') {
      flushWord();
      lines.push_back(line);
      line = "";
    } else if (c == ' ' || c == '\t') {
      flushWord();
    } else {
      word += c;
    }
  }
  flushWord();
  if (line.length()) lines.push_back(line);
  // Trailing blank lines would just add empty pages.
  while (lines.size() > 1 && lines.back().length() == 0) lines.pop_back();
  if (lines.empty()) lines.push_back("(empty file)");
  return lines;
}

// Screen geometry for a given text style.
struct TextLayout {
  const TextSettings &t;
  int w, h, cw, lh, cols, rows;
  explicit TextLayout(const TextSettings &ts) : t(ts) {
    w = ts.landscape ? SCREEN_H : SCREEN_W;
    h = ts.landscape ? SCREEN_W : SCREEN_H;
    cw = FONT_W * ts.size;
    lh = FONT_H * ts.size + ts.line_spacing;
    if (lh < 1) lh = 1;
    cols = max(1, (w - 2 * ts.margin) / cw);
    rows = max(1, (h - 2 * ts.margin + ts.line_spacing) / lh);
  }
  int blockHeight(int n) const { return n * lh - t.line_spacing; }
  int lineX(const String &s) const {
    int avail = w - 2 * t.margin, used = s.length() * cw;
    switch (t.align) {
      case Align::Center: return t.margin + (avail - used) / 2;
      case Align::Right:  return t.margin + avail - used;
      default:            return t.margin;
    }
  }
  int valignOffset(int n) const {
    int spare = (h - 2 * t.margin) - blockHeight(n);
    if (spare <= 0) return 0;
    switch (t.valign) {
      case VAlign::Middle: return spare / 2;
      case VAlign::Bottom: return spare;
      default:             return 0;
    }
  }
};

// Draws lines [first, first+count) into `c`. `yOff` scrolls the block up by
// that many pixels; `maxChars` (typewriter) limits how many characters show.
static void drawLines(GFXcanvas16 &c, const TextLayout &L, const std::vector<String> &lines,
                      size_t first, size_t count, int yOff, bool align, int maxChars = -1) {
  const TextSettings &t = L.t;
  c.setRotation(t.landscape ? 1 : 0);
  c.fillScreen(t.background);
  c.setTextSize(t.size);
  c.setTextColor(t.color);
  c.setTextWrap(false);
  int y0 = t.margin - yOff + (align ? L.valignOffset(count) : 0);
  for (size_t i = 0; i < count && first + i < lines.size(); i++) {
    int y = y0 + i * L.lh;
    if (y + L.lh < 0 || y >= L.h) continue;
    const String &s = lines[first + i];
    c.setCursor(L.lineX(s), y);
    if (maxChars < 0 || (int)s.length() <= maxChars) {
      c.print(s);
      if (maxChars >= 0) maxChars -= s.length();
    } else {
      c.print(s.substring(0, maxChars));
      break;
    }
  }
}

// Draws a short centred message (errors, "Insert SD card") and shows it.
static void showMessage(const String &msg, uint16_t color) {
  TextSettings ts;
  ts.color = color;
  ts.align = Align::Center;
  ts.valign = VAlign::Middle;
  ts.margin = 4;
  TextLayout L(ts);
  auto lines = wrapText(msg, L.cols);
  drawLines(screen::back(), L, lines, 0, min((size_t)L.rows, lines.size()), 0, true);
  screen::present();
}

static void showInsertCard() { showMessage("Insert SD card", ST77XX_YELLOW); }

static Play showError(const String &path, const char *msg) {
  Serial.printf("  %s: %s\n", path.c_str(), msg);
  showMessage(msg, ST77XX_RED);
  return waitSeconds(global.error_seconds) ? Play::Done : Play::CardGone;
}

// Reads up to MAX_TEXT_BYTES. The built-in font is ASCII, so any UTF-8
// multi-byte character is shown as a single '?'.
static String readText(File &f) {
  String out;
  out.reserve(min((size_t)f.size(), MAX_TEXT_BYTES));
  while (f.available() && out.length() < MAX_TEXT_BYTES) {
    uint8_t c = f.read();
    if ((c & 0xC0) == 0x80) continue;  // UTF-8 continuation byte
    if (c >= 0x80) c = '?';            // UTF-8 lead byte / non-ASCII
    if (c < 0x20 && c != '\n' && c != '\t') continue;
    out += (char)c;
  }
  return out;
}

// ---- Text playback ----------------------------------------------------------
static Play playText(const Item &it, bool only, Transition in) {
  File f = SD.open(it.path);
  if (!f) return showError(it.path, "Can't open file");
  const TextSettings &t = it.s.text;
  TextLayout L(t);
  std::vector<String> lines = wrapText(readText(f), L.cols);
  f.close();

  const size_t pages = (lines.size() + L.rows - 1) / L.rows;
  auto pageLines = [&](size_t p) { return min((size_t)L.rows, lines.size() - p * L.rows); };
  auto transition = [&](size_t p) {
    return p == 0 ? screen::transitionTo(in, it.s.transition_seconds, pollCard)
                  : screen::transitionTo(t.page_transition, t.page_transition_seconds, pollCard);
  };

  // --- Scroll: show the top, pause, glide to the bottom, pause.
  if (t.mode == TextMode::Scroll && pages > 1) {
    const int dist = L.blockHeight(lines.size()) - (L.h - 2 * t.margin);
    drawLines(screen::back(), L, lines, 0, lines.size(), 0, false);
    if (!transition(0) || !waitSeconds(t.hold_seconds)) return Play::CardGone;
    const uint32_t start = millis();
    int off = 0;
    while (off < dist) {
      off = min(dist, (int)((millis() - start) * t.scroll_speed / 1000));
      drawLines(screen::back(), L, lines, 0, lines.size(), off, false);
      screen::present();
      if (!pollCard()) return Play::CardGone;
    }
    return waitSeconds(t.hold_seconds) ? Play::Done : Play::CardGone;
  }

  // --- Page (and scroll text that fits on one screen).
  if (t.mode != TextMode::Typewriter) {
    for (size_t p = 0; p < pages; p++) {
      drawLines(screen::back(), L, lines, p * L.rows, pageLines(p), 0, true);
      if (!transition(p)) return Play::CardGone;
      if (only && pages == 1) return holdUntilRemoved();
      if (!waitSeconds(t.page_seconds)) return Play::CardGone;
    }
    return Play::Done;
  }

  // --- Typewriter: each page starts blank and types out.
  for (size_t p = 0; p < pages; p++) {
    const size_t n = pageLines(p);
    int total = 0;
    for (size_t i = 0; i < n; i++) total += lines[p * L.rows + i].length();
    drawLines(screen::back(), L, lines, p * L.rows, n, 0, true, 0);
    if (!transition(p)) return Play::CardGone;
    const uint32_t start = millis();
    int shown = 0;
    while (shown < total) {
      int want = min(total, (int)((millis() - start) * t.chars_per_second / 1000));
      if (want > shown) {
        shown = want;
        drawLines(screen::back(), L, lines, p * L.rows, n, 0, true, shown);
        screen::present();
      }
      if (!pollCard()) return Play::CardGone;
      delay(2);
    }
    if (only && pages == 1) return holdUntilRemoved();
    if (!waitSeconds(t.page_seconds)) return Play::CardGone;
  }
  return Play::Done;
}

// ---- BMP ----------------------------------------------------------------------
static uint16_t read16(File &f) {
  uint16_t v = 0;
  f.read((uint8_t *)&v, 2);  // BMP/AVI are little-endian, as is the ESP32
  return v;
}
static uint32_t read32(File &f) {
  uint32_t v = 0;
  f.read((uint8_t *)&v, 4);
  return v;
}

// Draws an uncompressed 24/32-bit BMP into `c`, centred (smaller images) or
// centre-cropped (larger). Returns an error message, or nullptr on success.
static const char *drawBmp(File &f, GFXcanvas16 &c, uint16_t bg) {
  if (read16(f) != 0x4D42) return "Not a BMP file";
  read32(f);
  read32(f);
  uint32_t dataOffset = read32(f);
  if (read32(f) < 40) return "Old BMP format";
  int32_t w = (int32_t)read32(f);
  int32_t h = (int32_t)read32(f);
  if (read16(f) != 1) return "Bad BMP";
  uint16_t bpp = read16(f);
  uint32_t compression = read32(f);
  if ((bpp != 24 && bpp != 32) || !(compression == 0 || (compression == 3 && bpp == 32)))
    return "Use 24-bit BMP";
  bool bottomUp = h > 0;
  if (h < 0) h = -h;
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return "Bad BMP size";

  c.setRotation(w > h ? 1 : 0);
  c.fillScreen(bg);
  const int sw = c.width(), sh = c.height();
  int dx = (sw - w) / 2, dy = (sh - h) / 2;
  int srcX0 = dx < 0 ? -dx : 0, srcY0 = dy < 0 ? -dy : 0;
  int outX = dx < 0 ? 0 : dx, outY = dy < 0 ? 0 : dy;
  int drawW = min((int)w - srcX0, sw - outX);
  int drawH = min((int)h - srcY0, sh - outY);

  const int bpx = bpp / 8;
  const uint32_t rowSize = ((uint32_t)w * bpx + 3) & ~3u;
  std::vector<uint8_t> raw(drawW * bpx);
  for (int y = 0; y < drawH; y++) {
    int srcY = srcY0 + y;
    f.seek(dataOffset + (bottomUp ? h - 1 - srcY : srcY) * rowSize + srcX0 * bpx);
    if (f.read(raw.data(), raw.size()) != (int)raw.size()) return "BMP read error";
    for (int x = 0; x < drawW; x++) {
      const uint8_t *p = &raw[x * bpx];  // B, G, R(, A)
      c.drawPixel(outX + x, outY + y, tft.color565(p[2], p[1], p[0]));
    }
  }
  return nullptr;
}

// ---- JPEG (images and video frames) -----------------------------------------
GFXcanvas16 *jpgTarget = nullptr;
int jpgX = 0, jpgY = 0, jpgW = 0, jpgH = 0;  // placement in target, may be < 0

// Copies a decoded block into the target canvas, clipped to the image and
// canvas. Portrait canvases take a fast row-copy path.
static int jpegDraw(JPEGDRAW *d) {
  GFXcanvas16 &c = *jpgTarget;
  const int x0 = jpgX + d->x, y0 = jpgY + d->y;
  const int cx0 = max(x0, max(jpgX, 0));
  const int cy0 = max(y0, max(jpgY, 0));
  const int cx1 = min(x0 + d->iWidthUsed, min(jpgX + jpgW, (int)c.width()));
  const int cy1 = min(y0 + d->iHeight, min(jpgY + jpgH, (int)c.height()));
  if (cx0 >= cx1 || cy0 >= cy1) return 1;
  if (c.getRotation() == 0) {
    uint16_t *buf = c.getBuffer();
    for (int y = cy0; y < cy1; y++)
      memcpy(buf + y * SCREEN_W + cx0, d->pPixels + (y - y0) * d->iWidth + (cx0 - x0),
             (cx1 - cx0) * 2);
  } else {
    for (int y = cy0; y < cy1; y++) {
      const uint16_t *src = d->pPixels + (y - y0) * d->iWidth + (cx0 - x0);
      for (int x = cx0; x < cx1; x++) c.drawPixel(x, y, *src++);
    }
  }
  return 1;
}

// Strongest power-of-2 downscale that still covers the canvas; centres the
// result. Returns the decode() option.
static int placeJpeg(GFXcanvas16 &c, int w, int h) {
  const int sw = c.width(), sh = c.height();
  int opt = 0, div = 1;
  const int opts[] = {JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH};
  for (int i = 0; i < 3; i++) {
    int d = 2 << i;
    if (w / d >= sw && h / d >= sh) { opt = opts[i]; div = d; }
  }
  jpgW = w / div;
  jpgH = h / div;
  jpgX = (sw - jpgW) / 2;
  jpgY = (sh - jpgH) / 2;
  jpgTarget = &c;
  return opt;
}

static int32_t jpgRead(JPEGFILE *jf, uint8_t *buf, int32_t len) {
  return ((File *)jf->fHandle)->read(buf, len);
}
static int32_t jpgSeek(JPEGFILE *jf, int32_t pos) {
  return ((File *)jf->fHandle)->seek(pos) ? pos : -1;
}
static void jpgClose(void *) {}

static const char *drawJpegFile(File &f, GFXcanvas16 &c, uint16_t bg) {
  if (!jpeg.open(&f, f.size(), jpgClose, jpgRead, jpgSeek, jpegDraw)) return "Unsupported JPEG";
  c.setRotation(jpeg.getWidth() > jpeg.getHeight() ? 1 : 0);
  c.fillScreen(bg);
  int opt = placeJpeg(c, jpeg.getWidth(), jpeg.getHeight());
  int ok = jpeg.decode(0, 0, opt);
  jpeg.close();
  return ok ? nullptr : "Use baseline JPEG";  // progressive JPEGs fail here
}

static Play playImage(const Item &it, bool only, Transition in) {
  File f = SD.open(it.path);
  if (!f) return showError(it.path, "Can't open file");
  GFXcanvas16 &c = screen::back();
  const char *err = it.kind == Kind::Bmp ? drawBmp(f, c, it.s.image.background)
                                         : drawJpegFile(f, c, it.s.image.background);
  f.close();
  if (err) return cardPresent() ? showError(it.path, err) : Play::CardGone;
  if (!screen::transitionTo(in, it.s.transition_seconds, pollCard)) return Play::CardGone;
  if (only) return holdUntilRemoved();
  return waitSeconds(it.s.image.duration_seconds) ? Play::Done : Play::CardGone;
}

// ---- AVI (MJPEG) video --------------------------------------------------------
static bool isFourcc(const char *a, const char *b) { return memcmp(a, b, 4) == 0; }

struct AviInfo {
  uint32_t usPerFrame = 66667;  // 15 fps if the header doesn't say
  uint32_t width = 0, height = 0;
  uint32_t moviStart = 0, moviEnd = 0;
};

static const char *parseAvi(File &f, AviInfo &info) {
  char id[4];
  f.seek(0);
  if (f.read((uint8_t *)id, 4) != 4 || !isFourcc(id, "RIFF")) return "Not an AVI file";
  read32(f);
  if (f.read((uint8_t *)id, 4) != 4 || !isFourcc(id, "AVI ")) return "Not an AVI file";
  uint32_t pos = 12, end = f.size();
  while (pos + 8 <= end) {
    f.seek(pos);
    f.read((uint8_t *)id, 4);
    uint32_t size = read32(f);
    if (isFourcc(id, "LIST")) {
      char type[4];
      f.read((uint8_t *)type, 4);
      if (isFourcc(type, "movi")) {
        info.moviStart = pos + 12;
        info.moviEnd = min(pos + 8 + size, end);
        break;
      }
      pos += 12;  // descend into hdrl / strl / etc.
      continue;
    }
    if (isFourcc(id, "avih")) {
      uint32_t us = read32(f);
      if (us > 0) info.usPerFrame = us;
      f.seek(pos + 8 + 32);  // dwWidth, dwHeight
      info.width = read32(f);
      info.height = read32(f);
    }
    pos += 8 + size + (size & 1);
  }
  return info.moviStart ? nullptr : "Bad AVI file";
}

// Finds the next video frame chunk at or after `pos`. Returns false at end.
static bool nextFrame(File &f, const AviInfo &info, uint32_t &pos, uint32_t &dataPos,
                      uint32_t &size) {
  char id[4];
  while (pos + 8 <= info.moviEnd) {
    f.seek(pos);
    if (f.read((uint8_t *)id, 4) != 4) return false;
    size = read32(f);
    if (isFourcc(id, "LIST")) { pos += 12; continue; }  // 'rec ' groups
    dataPos = pos + 8;
    pos += 8 + size + (size & 1);
    if (id[2] == 'd' && (id[3] == 'c' || id[3] == 'b') && size > 0) return true;
  }
  return false;
}

static bool decodeFrame(File &f, uint32_t dataPos, uint32_t size, GFXcanvas16 &c,
                        int &scaleOpt, bool place) {
  if (size > MAX_FRAME_BYTES) return false;
  f.seek(dataPos);
  if (f.read(frameBuf, size) != (int)size) return false;
  if (!jpeg.openRAM(frameBuf, size, jpegDraw)) return false;
  if (place) scaleOpt = placeJpeg(c, jpeg.getWidth(), jpeg.getHeight());
  jpgTarget = &c;
  jpeg.decode(0, 0, scaleOpt);
  jpeg.close();
  return true;
}

static Play playVideo(const Item &it, bool only, Transition in) {
  File f = SD.open(it.path);
  if (!f) return showError(it.path, "Can't open file");
  AviInfo info;
  if (const char *err = parseAvi(f, info)) {
    f.close();
    return showError(it.path, err);
  }
  const uint32_t us = info.usPerFrame / it.s.video.speed;
  Serial.printf("  %ux%u, %.1f fps x%.2f speed, %u loop(s)\n", (unsigned)info.width,
                (unsigned)info.height, 1e6f / info.usPerFrame, it.s.video.speed,
                (unsigned)it.s.video.loops);

  // First frame goes into the back buffer and transitions in.
  uint32_t pos = info.moviStart, dataPos, size;
  int scaleOpt = 0;
  GFXcanvas16 &first = screen::back();
  first.setRotation(info.width > info.height ? 1 : 0);
  first.fillScreen(ST77XX_BLACK);
  if (!nextFrame(f, info, pos, dataPos, size) ||
      !decodeFrame(f, dataPos, size, first, scaleOpt, true)) {
    f.close();
    return cardPresent() ? showError(it.path, "Use MJPEG AVI") : Play::CardGone;
  }
  if (!screen::transitionTo(in, it.s.transition_seconds, pollCard)) {
    f.close();
    return Play::CardGone;
  }

  // The rest decode straight into the front buffer, which keeps the borders.
  GFXcanvas16 &c = screen::front();
  uint32_t frames = 1, shown = 1, dropped = 0;
  Play result = Play::Done;
  const uint32_t tStart = micros();
  for (uint16_t loop = 0; loop < it.s.video.loops && result == Play::Done; loop++) {
    const uint32_t t0 = micros();
    uint32_t idx = 0;
    if (loop > 0) pos = info.moviStart;
    else idx = 1;  // frame 0 is already on screen
    while (nextFrame(f, info, pos, dataPos, size)) {
      if (!pollCard()) { result = Play::CardGone; break; }
      const uint32_t due = idx++ * us;
      frames++;
      if (micros() - t0 > due + us) { dropped++; continue; }  // behind: drop
      if (!decodeFrame(f, dataPos, size, c, scaleOpt, false)) {
        if (!cardPresent()) { result = Play::CardGone; break; }
        dropped++;
        continue;
      }
      screen::push(c);
      shown++;
      while (micros() - t0 < due + us) delayMicroseconds(200);
    }
  }
  f.close();
  if (result == Play::CardGone) return result;
  Serial.printf("  %u frames: %u shown, %u dropped (%.1f fps achieved)\n", (unsigned)frames,
                (unsigned)shown, (unsigned)dropped,
                shown * 1e6f / max(1u, (unsigned)(micros() - tStart)));
  (void)only;
  return Play::Done;
}

// ---- Playlist building ------------------------------------------------------
static bool kindFor(const String &name, Kind &kind) {
  String n = name;
  n.toLowerCase();
  if (n.endsWith(".txt")) kind = Kind::Text;
  else if (n.endsWith(".bmp")) kind = Kind::Bmp;
  else if (n.endsWith(".jpg") || n.endsWith(".jpeg")) kind = Kind::Jpeg;
  else if (n.endsWith(".avi")) kind = Kind::Video;
  else return false;
  return true;
}

// Every supported file in the root, by name (case-insensitive), with the
// default settings. Hidden files (e.g. macOS "._foo.jpg") are skipped.
static void scanRoot(const Settings &defaults) {
  File root = SD.open("/");
  if (!root || !root.isDirectory()) return;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = f.name();
    Kind kind;
    if (!f.isDirectory() && name[0] != '.' && kindFor(name, kind))
      playlist.push_back({"/" + name, kind, defaults});
    f.close();
  }
  root.close();
  std::sort(playlist.begin(), playlist.end(), [](const Item &a, const Item &b) {
    return strcasecmp(a.path.c_str(), b.path.c_str()) < 0;
  });
}

// Loads /config.json (if any) and builds the playlist from it. Returns a
// message to show on screen if something is wrong, or "" if all is fine.
static String loadConfigAndPlaylist() {
  global = GlobalSettings();
  playlist.clear();
  Settings defaults;

  File f = SD.open(CONFIG_PATH);
  if (!f) {
    Serial.println("No config.json, using defaults");
    scanRoot(defaults);
    return "";
  }
  if (f.size() > MAX_CONFIG_BYTES) {
    f.close();
    scanRoot(defaults);
    return "config.json too big";
  }
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, f);
  f.close();
  if (de || !doc.is<JsonObject>()) {
    Serial.printf("config.json: %s\n", de ? de.c_str() : "must be a { ... } object");
    scanRoot(defaults);
    return String("config.json error: ") + (de ? de.c_str() : "not an object") +
           " (using defaults)";
  }

  std::vector<String> warnings;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  applyGlobal(root, global, warnings);
  applySettings(root, defaults, warnings);

  JsonVariantConst pl = root["playlist"];
  if (pl.isNull()) {
    scanRoot(defaults);
  } else if (!pl.is<JsonArrayConst>()) {
    warnings.push_back("playlist: must be a list [ ... ]");
    scanRoot(defaults);
  } else {
    int i = 0;
    for (JsonVariantConst e : pl.as<JsonArrayConst>()) {
      String where = "playlist[" + String(i++) + "]";
      Item it;
      it.s = defaults;
      String file;
      if (e.is<const char *>()) {
        file = e.as<const char *>();
      } else if (e.is<JsonObjectConst>()) {
        JsonObjectConst o = e.as<JsonObjectConst>();
        file = o["file"] | "";
        for (JsonPairConst kv : o) {
          const char *k = kv.key().c_str();
          if (k[0] != '_' && strcmp(k, "file") && strcmp(k, "transition") &&
              strcmp(k, "transition_seconds") && strcmp(k, "text") && strcmp(k, "image") &&
              strcmp(k, "video"))
            warnings.push_back(where + "." + k + ": unknown setting (ignored)");
        }
        applySettings(o, it.s, warnings);
      } else {
        warnings.push_back(where + ": must be \"name\" or { \"file\": ... }");
        continue;
      }
      if (file.isEmpty()) { warnings.push_back(where + ": missing \"file\""); continue; }
      it.path = file.startsWith("/") ? file : "/" + file;
      if (!kindFor(it.path, it.kind)) {
        warnings.push_back(where + ": unsupported file type " + file);
        continue;
      }
      if (!SD.exists(it.path)) {
        warnings.push_back(where + ": file not found " + file);
        continue;
      }
      playlist.push_back(it);
    }
  }

  Serial.printf("config.json loaded, %u warning(s)\n", (unsigned)warnings.size());
  for (auto &w : warnings) Serial.printf("  warning: %s\n", w.c_str());
  if (warnings.empty()) return "";
  String msg = "config.json: " + warnings[0];
  if (warnings.size() > 1) msg += " (+" + String(warnings.size() - 1) + " more, see serial)";
  return msg;
}

static void shufflePlaylist() {
  for (size_t i = playlist.size(); i > 1; i--) std::swap(playlist[i - 1], playlist[esp_random() % i]);
}

static bool mountCard() {
  SD.end();
  return SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ) && SD.cardType() != CARD_NONE;
}

static void unmountCard() {
  SD.end();
  mounted = false;
  Serial.println("SD card removed");
  showInsertCard();
}

static void setBacklight(uint8_t v) { ledcWrite(PIN_TFT_LED, v); }

// ---- Arduino ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_SD_CS, OUTPUT);  // deselect the card before the bus is used
  digitalWrite(PIN_SD_CS, HIGH);

  ledcAttach(PIN_TFT_LED, 5000, 8);  // backlight PWM (arduino-esp32 3.x API)
  setBacklight(global.backlight);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);

  // Most red-PCB 1.8" modules are BLACKTAB. If colours are inverted or there
  // is a garbage strip along one edge, try INITR_GREENTAB or INITR_REDTAB.
  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(TFT_SPI_HZ);
  tft.setRotation(0);  // panel stays native; canvases handle orientation

  frameBuf = (uint8_t *)malloc(MAX_FRAME_BYTES);
  if (!screen::begin() || !frameBuf) {
    tft.fillScreen(ST77XX_RED);
    Serial.println("Out of memory");
    for (;;) delay(1000);
  }
  showInsertCard();
}

void loop() {
  if (!mounted) {
    if (!mountCard()) {
      delay(CARD_POLL_MS);
      return;
    }
    mounted = true;
    lastPollMs = millis();
    Serial.println("SD card mounted");
    String problem = loadConfigAndPlaylist();
    setBacklight(global.backlight);
    if (global.shuffle) shufflePlaylist();
    Serial.printf("Playlist: %u item(s)%s%s\n", (unsigned)playlist.size(),
                  global.shuffle ? ", shuffled" : "", global.loop ? ", looping" : "");
    for (auto &it : playlist)
      Serial.printf("  %s (transition %s)\n", it.path.c_str(), transitionName(it.s.transition));
    if (problem.length()) {
      showMessage(problem, ST77XX_ORANGE);
      if (!waitSeconds(max(3.0f, global.error_seconds))) { unmountCard(); return; }
    }
    current = 0;
    playedOnce = false;
    if (playlist.empty()) showMessage("No playable files", ST77XX_RED);
  }

  if (playlist.empty()) {
    if (!waitMs(CARD_POLL_MS)) unmountCard();
    return;
  }

  const Item &it = playlist[current];
  const bool only = playlist.size() == 1;
  // A single video looping on its own shouldn't transition into itself.
  const Transition in = (only && playedOnce) ? Transition::None : it.s.transition;
  Serial.printf("Playing %s\n", it.path.c_str());

  Play r;
  switch (it.kind) {
    case Kind::Text:  r = playText(it, only, in); break;
    case Kind::Bmp:
    case Kind::Jpeg:  r = playImage(it, only, in); break;
    case Kind::Video: r = playVideo(it, only, in); break;
    default:          r = Play::Done; break;
  }
  playedOnce = true;
  if (r == Play::CardGone) {
    unmountCard();
    return;
  }

  if (++current >= playlist.size()) {
    if (!global.loop) {
      Serial.println("Playlist finished (loop is off)");
      holdUntilRemoved();
      unmountCard();
      return;
    }
    current = 0;
    if (global.shuffle && playlist.size() > 2) {
      // Reshuffle, but don't repeat the item that just played.
      String last = playlist.back().path;
      shufflePlaylist();
      if (playlist.front().path == last) std::swap(playlist.front(), playlist.back());
    }
  }
}
