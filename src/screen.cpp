#include "screen.h"

namespace screen {
namespace {

GFXcanvas16 *bufA = nullptr, *bufB = nullptr;
GFXcanvas16 *frontBuf = nullptr, *backBuf = nullptr;
uint16_t line[SCREEN_W];

enum Dir { L, R, U, D };

// Directions in the config are as the viewer sees them. Canvases are stored
// in native portrait layout, so for a landscape (rotation 1) screen map them:
// GFX rotation 1 puts logical x on native y and logical y on native -x.
Dir nativeDir(Dir d, uint8_t rotation) {
  if (rotation != 1) return d;
  switch (d) {
    case L: return U;
    case R: return D;
    case U: return R;
    default: return L;
  }
}

// RGB565 blend: alpha 0 = a, 32 = b.
inline uint16_t blend(uint16_t a, uint16_t b, uint32_t alpha) {
  uint32_t inv = 32 - alpha;
  uint32_t rb = (((b & 0xF81F) * alpha + (a & 0xF81F) * inv) >> 5) & 0xF81F;
  uint32_t g  = (((b & 0x07E0) * alpha + (a & 0x07E0) * inv) >> 5) & 0x07E0;
  return rb | g;
}

// 0-255 pseudo-random value per 4x4 block, fixed per transition.
inline uint8_t blockNoise(int x, int y, uint32_t seed) {
  uint32_t h = ((x >> 2) * 73856093u) ^ ((y >> 2) * 19349663u) ^ seed;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  return h >> 24;
}

float ease(float t) { return t * t * (3 - 2 * t); }  // smoothstep

}  // namespace

bool begin() {
  bufA = new GFXcanvas16(SCREEN_W, SCREEN_H);
  bufB = new GFXcanvas16(SCREEN_W, SCREEN_H);
  if (!bufA->getBuffer() || !bufB->getBuffer()) return false;
  frontBuf = bufA;
  backBuf = bufB;
  frontBuf->fillScreen(0);
  backBuf->fillScreen(0);
  return true;
}

GFXcanvas16 &front() { return *frontBuf; }
GFXcanvas16 &back() { return *backBuf; }

void push(GFXcanvas16 &c) {
  tft.startWrite();
  tft.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
  tft.writePixels(c.getBuffer(), SCREEN_W * SCREEN_H, true, false);
  tft.endWrite();
}

void present() {
  push(*backBuf);
  std::swap(frontBuf, backBuf);
}

bool transitionTo(Transition t, float seconds, bool (*keepGoing)()) {
  if (t == Transition::Random) {
    // Anything except None/Random.
    t = (Transition)(1 + esp_random() % ((int)Transition::Random - 1));
  }
  if (t == Transition::None || seconds <= 0) {
    present();
    return true;
  }

  const uint16_t *a = frontBuf->getBuffer();
  const uint16_t *b = backBuf->getBuffer();
  const uint8_t rot = backBuf->getRotation();
  const uint32_t seed = esp_random();
  const uint32_t ms = seconds * 1000;
  const uint32_t start = millis();
  constexpr int W = SCREEN_W, H = SCREEN_H;

  Dir dir = L;
  switch (t) {
    case Transition::WipeLeft: case Transition::SlideLeft: dir = L; break;
    case Transition::WipeRight: case Transition::SlideRight: dir = R; break;
    case Transition::WipeUp: case Transition::SlideUp: dir = U; break;
    case Transition::WipeDown: case Transition::SlideDown: case Transition::Blinds: dir = D; break;
    default: break;
  }
  dir = nativeDir(dir, rot);

  for (;;) {
    uint32_t el = millis() - start;
    if (el >= ms) break;
    const float p = ease((float)el / ms);

    tft.startWrite();
    tft.setAddrWindow(0, 0, W, H);
    for (int y = 0; y < H; y++) {
      const uint16_t *ra = a + y * W, *rb = b + y * W;
      switch (t) {
        case Transition::Crossfade: {
          uint32_t al = p * 32;
          for (int x = 0; x < W; x++) line[x] = blend(ra[x], rb[x], al);
          break;
        }
        case Transition::Fade: {  // out to black, then in from black
          const uint16_t *src = p < 0.5f ? ra : rb;
          uint32_t al = p < 0.5f ? (1 - 2 * p) * 32 : (2 * p - 1) * 32;
          for (int x = 0; x < W; x++) line[x] = blend(0, src[x], al);
          break;
        }
        case Transition::WipeLeft: case Transition::WipeRight:
        case Transition::WipeUp: case Transition::WipeDown: {
          for (int x = 0; x < W; x++) {
            bool nw;
            switch (dir) {
              case L: nw = x >= W - p * W; break;
              case R: nw = x < p * W; break;
              case U: nw = y >= H - p * H; break;
              default: nw = y < p * H; break;
            }
            line[x] = nw ? rb[x] : ra[x];
          }
          break;
        }
        case Transition::SlideLeft: case Transition::SlideRight:
        case Transition::SlideUp: case Transition::SlideDown: {
          // Old screen moves out in `dir`, new one follows it in.
          if (dir == L || dir == R) {
            int k = p * W;
            for (int x = 0; x < W; x++) {
              if (dir == L) line[x] = x < W - k ? ra[x + k] : rb[x - (W - k)];
              else          line[x] = x >= k ? ra[x - k] : rb[x + W - k];
            }
          } else {
            int k = p * H;
            const uint16_t *src;
            if (dir == U) src = y < H - k ? a + (y + k) * W : b + (y - (H - k)) * W;
            else          src = y >= k ? a + (y - k) * W : b + (y + H - k) * W;
            memcpy(line, src, sizeof(line));
          }
          break;
        }
        case Transition::Dissolve: {
          uint32_t thr = p * 256;
          for (int x = 0; x < W; x++) line[x] = blockNoise(x, y, seed) < thr ? rb[x] : ra[x];
          break;
        }
        case Transition::Blinds: {  // 16 px slats opening along `dir`
          for (int x = 0; x < W; x++) {
            int pos;
            switch (dir) {
              case L: pos = 15 - (x & 15); break;
              case R: pos = x & 15; break;
              case U: pos = 15 - (y & 15); break;
              default: pos = y & 15; break;
            }
            line[x] = pos < p * 16 ? rb[x] : ra[x];
          }
          break;
        }
        default:
          memcpy(line, rb, sizeof(line));
          break;
      }
      tft.writePixels(line, W, true, false);
    }
    tft.endWrite();

    if (keepGoing && !keepGoing()) return false;
  }
  present();
  return true;
}

}  // namespace screen
