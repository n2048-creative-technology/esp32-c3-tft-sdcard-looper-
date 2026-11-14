# XIAO ESP32-C3 + 128x160 SPI TFT

PlatformIO project for driving a 1.8" 128x160 SPI TFT (ST7735 controller)
with an onboard SD card reader from a Seeed Studio XIAO ESP32-C3, using the
Adafruit ST7735 + GFX libraries over hardware SPI.

## What it does

Plays the media on the SD card as a **looping playlist**. Only the content
is shown, never the file name.

- **Without a `config.json`**, it plays every supported file in the card's
  root directory in alphabetical order (prefix names with `01-`, `02-`… to set
  the order), using the defaults below.
- **With a [`config.json`](#configuration-configjson)** you set the order,
  timing, transitions, text style and more, globally or per file. The file is
  re-read whenever a card is inserted, so you change behaviour by editing it,
  with no re-flashing.

| Type   | Extensions      | Default behaviour                                              |
|--------|-----------------|----------------------------------------------------------------|
| Text   | `.txt`          | Word-wrapped, 21 x 20 characters per page, 5 s per page (first 16 KB). Can also scroll or type out |
| Image  | `.bmp`          | Uncompressed 24/32-bit BMP, shown for 5 s                      |
| Image  | `.jpg` `.jpeg`  | Baseline JPEG, shown for 5 s                                   |
| Video  | `.avi`          | MJPEG video (no audio), plays once at the frame rate in the file |

- If the playlist holds a **single** item, a one-page text or an image just
  stays on screen, and a video loops.
- With no card inserted it shows **"Insert SD card"** and checks once a
  second, so cards can be inserted, removed or swapped at any time without a
  reset (even mid-video).
- Files it can't play (e.g. a progressive JPEG) show a short red message for
  2 s, then the playlist moves on. A card with no supported files shows
  "No playable files".
- Hidden files (names starting with `.`, such as macOS `._*` files) and
  subfolders are ignored.

The card must be FAT16/FAT32 formatted. Only plain ASCII text is shown; other
characters (accents, emoji) appear as `?`.

## Configuration (`config.json`)

Put a file named **`config.json`** in the root of the SD card. Everything in
it is optional: leave out any key to keep its default. A complete working
example is in [`sd-card/config.json`](sd-card/config.json), and more are in
[`config-examples/`](config-examples/).

```json
{
  "transition": "crossfade",
  "transition_seconds": 0.6,
  "text":  { "mode": "scroll", "scroll_speed": 25, "color": "#40FF80" },
  "image": { "duration_seconds": 8 },
  "playlist": [
    "title.txt",
    { "file": "story.txt", "text": { "mode": "typewriter", "chars_per_second": 20 } },
    { "file": "clip.avi",  "transition": "wipe_left", "video": { "speed": 2, "loops": 3 } },
    "photo.jpg"
  ]
}
```

### Structure

- **Top level:** global options, plus the *default* `transition`, `text`,
  `image` and `video` settings for every item.
- **`playlist`** (optional): the files to play, in order. An entry is either a
  file name (`"photo.jpg"`) or an object `{ "file": "...", ... }` that
  overrides any of `transition`, `transition_seconds`, `text`, `image` and
  `video` for that one file. Only the keys you give are changed; the rest come
  from the defaults. Without `playlist`, every supported file in the root
  plays alphabetically. Files can be in subfolders (`"clips/a.avi"`), and the
  same file can appear more than once.
- Keys starting with `_` (e.g. `"_comment"`) are ignored, so you can annotate
  the file. `//` and `/* */` comments are also accepted.

### Global options (top level only)

| Key             | Default | Values                | Meaning                                        |
|-----------------|---------|-----------------------|------------------------------------------------|
| `backlight`     | `200`   | 0–255                 | Backlight brightness (0 = off)                 |
| `shuffle`       | `false` | `true` / `false`      | Random order, reshuffled every time round      |
| `loop`          | `true`  | `true` / `false`      | `false` = stop and stay on the last item       |
| `error_seconds` | `2`     | 0–60                  | How long "can't play this file" messages show  |

### Transitions (top level or per playlist entry)

| Key                  | Default  | Values                      | Meaning                          |
|----------------------|----------|-----------------------------|----------------------------------|
| `transition`         | `"none"` | see list below              | Effect when this item appears    |
| `transition_seconds` | `0.5`    | 0–10                        | Effect duration                  |

`none`, `fade` (through black), `crossfade`, `wipe_left`, `wipe_right`,
`wipe_up`, `wipe_down`, `slide_left`, `slide_right`, `slide_up`, `slide_down`,
`dissolve`, `blinds`, `random` (a different random effect each time).
Directions are as you see them, including when the screen is in landscape.

### `text` settings

| Key                       | Default   | Values                              | Meaning |
|---------------------------|-----------|-------------------------------------|---------|
| `mode`                    | `"page"`  | `page`, `scroll`, `typewriter`      | **page:** one screen at a time. **scroll:** shows the top, pauses, glides smoothly to the end, pauses. **typewriter:** each page types out character by character |
| `page_seconds`            | `5`       | 0.1–3600                            | Time each page stays (page mode; after typing finishes in typewriter mode) |
| `scroll_speed`            | `20`      | 1–500 pixels/second                 | Scroll mode speed (a text line is 8 px at size 1) |
| `hold_seconds`            | `1.5`     | 0–3600                              | Scroll mode pause at the start and end |
| `chars_per_second`        | `30`      | 1–1000                              | Typewriter speed |
| `size`                    | `1`       | 1–4                                 | Font scale: 1 = 21x20 chars, 2 = 10x10, 3 = 7x6, 4 = 5x5 (portrait, no margin) |
| `color`                   | `"white"` | `"#RRGGBB"`, `"#RGB"` or a name     | Text colour |
| `background`              | `"black"` | same                                | Background colour |
| `align`                   | `"left"`  | `left`, `center`, `right`           | Horizontal alignment of each line |
| `valign`                  | `"top"`   | `top`, `middle`, `bottom`           | Vertical position when the text is shorter than the screen |
| `margin`                  | `0`       | 0–40 pixels                         | Empty border on every side |
| `line_spacing`            | `0`       | -4–40 pixels                        | Extra space between lines |
| `orientation`             | `"portrait"` | `portrait`, `landscape`          | Landscape gives 26 columns x 16 lines at size 1 |
| `page_transition`         | `"none"`  | any transition                      | Effect between pages of the same file (page & typewriter modes) |
| `page_transition_seconds` | `0.3`     | 0–10                                | Duration of that effect |

Colour names: `black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`,
`magenta`, `orange`, `gray`/`grey`.

A text that fits on one screen in scroll mode is shown like a page.

### `image` settings

| Key                | Default   | Values        | Meaning                                       |
|--------------------|-----------|---------------|-----------------------------------------------|
| `duration_seconds` | `5`       | 0.1–3600      | How long the image stays                      |
| `background`       | `"black"` | colour        | Fills the area around images smaller than the screen |

### `video` settings

| Key     | Default | Values   | Meaning                                                      |
|---------|---------|----------|--------------------------------------------------------------|
| `speed` | `1.0`   | 0.1–4    | Playback speed (`0.5` = half speed, `2` = double). Above the decoder's limit (~15–20 fps actual), frames are dropped to keep the timing |
| `loops` | `1`     | 1–1000   | How many times to play the video before moving on            |

### When something is wrong

- **JSON syntax error** (missing comma, bracket, quote): an orange
  "config.json error: …" message shows for 3 s, then the card plays **as if
  there were no config** (all files, defaults).
- **Bad values or unknown keys** (e.g. `"transition": "spin"`, `"size": 9`, a
  missing file): each problem is skipped or clamped to the valid range, the
  first one is shown on screen for 3 s, and **all of them are listed on the
  serial monitor** (`pio device monitor`). Everything else in the file still
  applies.
- The serial monitor also prints the final playlist with each item's
  transition, which is handy for checking that your config was read as intended.

Files in `config-examples/` (copy one to the card as `config.json`):

| File                     | Shows                                                   |
|--------------------------|---------------------------------------------------------|
| `minimal.json`           | Just a few defaults, no playlist                        |
| `shuffle-slideshow.json` | Shuffled order, random transitions, big centred typewriter text |
| `with-errors.json`       | Bad values and missing files, to see the warnings       |
| `syntax-error.json`      | Broken JSON, to see the fallback-to-defaults behaviour  |

## Media formats

### Images and video sizing

- Best size: **128x160** (portrait) or **160x128** (landscape, which rotates
  the screen automatically).
- Smaller media is centred on black. Larger media is centre-cropped. JPEGs and
  video frames are first shrunk by 1/2, 1/4 or 1/8, whichever still covers
  the screen, so a big photo shows mostly whole, but resizing to the screen
  size first gives the best result.

### Converting your own files (ffmpeg)

Every command below was checked to produce files this firmware accepts. Each
one comes in two flavours:

- **Fill** (`increase,crop`): scales to cover the whole screen and crops the
  overflow. No borders, but the edges get cut off.
- **Fit** (`decrease,pad`): scales so the whole picture fits and adds black
  bars. Nothing is cut off.

For **portrait** use `128:160` everywhere in the filter, and for **landscape**
use `160:128`. Orientation is detected from the file, and the screen rotates
to match.

#### Images → JPEG (smallest files, recommended)

```sh
# Portrait, fill
ffmpeg -i photo.png -vf "scale=128:160:force_original_aspect_ratio=increase,crop=128:160" \
       -q:v 3 -pix_fmt yuvj420p photo.jpg

# Landscape, fit
ffmpeg -i photo.png -vf "scale=160:128:force_original_aspect_ratio=decrease,pad=160:128:(ow-iw)/2:(oh-ih)/2" \
       -q:v 3 -pix_fmt yuvj420p photo.jpg
```

`-q:v` sets JPEG quality: 2 is best and 31 is worst, and 2–5 looks good on this
screen. ffmpeg always writes **baseline** JPEGs, which is what the decoder
needs. Progressive JPEGs (common from web downloads) won't play, so re-encode
them with the command above.

#### Images → BMP (lossless)

```sh
# Portrait, fill
ffmpeg -i photo.png -vf "scale=128:160:force_original_aspect_ratio=increase,crop=128:160" \
       -pix_fmt bgr24 photo.bmp

# Landscape, fit
ffmpeg -i photo.png -vf "scale=160:128:force_original_aspect_ratio=decrease,pad=160:128:(ow-iw)/2:(oh-ih)/2" \
       -pix_fmt bgr24 photo.bmp
```

`-pix_fmt bgr24` is what makes it a 24-bit uncompressed BMP. Without it, ffmpeg
may write a format the firmware rejects ("Use 24-bit BMP").

#### Videos (and animated GIFs) → MJPEG AVI

```sh
# Portrait, fill
ffmpeg -i clip.mp4 -vf "scale=128:160:force_original_aspect_ratio=increase,crop=128:160,fps=15" \
       -an -c:v mjpeg -q:v 5 -pix_fmt yuvj420p clip.avi

# Landscape, fit
ffmpeg -i clip.mp4 -vf "scale=160:128:force_original_aspect_ratio=decrease,pad=160:128:(ow-iw)/2:(oh-ih)/2,fps=15" \
       -an -c:v mjpeg -q:v 5 -pix_fmt yuvj420p clip.avi
```

| Option                | Why                                                          |
|-----------------------|--------------------------------------------------------------|
| `-c:v mjpeg`          | The only video codec the firmware decodes                    |
| `-an`                 | Drops audio (the board has no speaker, and audio wastes card bandwidth) |
| `fps=15`              | 15–20 fps is what the C3 can sustain at full screen. Higher rates just drop frames |
| `-q:v 5`              | 2–4 is sharper but bigger, and 6–10 is smaller and decodes faster. Each frame must be under 48 KB |
| `-pix_fmt yuvj420p`   | Standard JPEG chroma format, which decodes fastest           |

Useful extras, added before the output file name:

- `-ss 00:01:30 -t 10` takes a 10 s excerpt starting at 1:30.
- `-vf "...,fps=15,hflip"` mirrors the picture. Use `transpose=1` to rotate 90°
  before scaling.

The same command works for animated GIFs: `ffmpeg -i anim.gif ...` as above.

#### A still frame from a video → JPEG

```sh
ffmpeg -ss 00:00:05 -i clip.mp4 -frames:v 1 \
       -vf "scale=128:160:force_original_aspect_ratio=increase,crop=128:160" \
       -q:v 3 -pix_fmt yuvj420p frame.jpg
```

#### Convert a whole folder at once

```sh
mkdir -p sd
n=1
for f in *.mp4 *.mov; do
  [ -e "$f" ] || continue
  ffmpeg -i "$f" -vf "scale=128:160:force_original_aspect_ratio=increase,crop=128:160,fps=15" \
         -an -c:v mjpeg -q:v 5 -pix_fmt yuvj420p "sd/$(printf %02d $n)-${f%.*}.avi"
  n=$((n+1))
done
for f in *.png *.jpg *.jpeg *.heic; do
  [ -e "$f" ] || continue
  ffmpeg -i "$f" -vf "scale=128:160:force_original_aspect_ratio=increase,crop=128:160" \
         -q:v 3 -pix_fmt yuvj420p "sd/$(printf %02d $n)-${f%.*}.jpg"
  n=$((n+1))
done
```

The numeric prefixes set the playlist order. Copy the contents of `sd/` to the
card root.

## Test files

`sd-card/` holds a ready-made set. **Copy its contents to the root of the SD
card**. Delete or rename `config.json` on the card to see the default
behaviour instead:

| File                          | Tests                                            |
|-------------------------------|--------------------------------------------------|
| `config.json`                 | Test config: explicit playlist touring every text mode, most transitions, video speed and loops, per-file colours and layout |
| `00-title.txt`                | Large centred title card (size 3, coloured background) |
| `01-welcome.txt`              | Short text (typewriter mode in the test config)  |
| `02-random-portrait.bmp`      | Random 128x160 BMP with white border (alignment) |
| `03-random.jpg`               | 128x160 JPEG                                     |
| `04-life-portrait.avi`        | Random Game-of-Life video, 128x160, 15 fps, 10 s |
| `05-long-text.txt`            | Multi-page text, word wrap, a hard-broken long word (scroll mode in the test config) |
| `06-landscape-160x128.bmp`    | Landscape image (screen rotates)                 |
| `07-big-photo-1024x1280.jpg`  | Large JPEG (downscaled 1/8 then centre-cropped)  |
| `08-mandelbrot-landscape.avi` | Landscape video, 160x128, 15 fps, 8 s            |
| `09-unicode.txt`              | Non-ASCII characters shown as `?` (landscape, centred in the test config) |
| `10-small-centred.bmp`        | 64x80 image centred on black                     |
| `11-counter-20fps.avi`        | ffmpeg test pattern with frame counter, 20 fps   |

Generators (to make new random ones):

```sh
python3 tools/make_test_image.py out.bmp [--seed N]           # random 128x160 BMP, no extra packages
tools/make_test_video.sh out.avi [seconds] [fps] [seed]       # random 128x160 MJPEG AVI, needs ffmpeg
```

## Wiring

| Display pin | Function                          | XIAO pin | GPIO   |
|-------------|-----------------------------------|----------|--------|
| VCC         | Power                             | 3V3      | –      |
| GND         | Ground                            | GND      | –      |
| CS          | Chip select                       | D1       | GPIO3  |
| RESET       | Reset                             | D3       | GPIO5  |
| A0          | Data / command select (a.k.a. DC/RS) | D2    | GPIO4  |
| SDA         | SPI data in (MOSI)                | D10      | GPIO10 |
| SCK         | SPI clock                         | D8       | GPIO8  |
| LED         | Backlight (PWM dimmable)          | D4       | GPIO6  |
| SD_CS       | SD card chip select               | D5       | GPIO7  |
| SD_MOSI     | SD data in                        | D10      | GPIO10 |
| SD_MISO     | SD data out                       | D9       | GPIO9  |
| SD_SCK      | SD clock                          | D8       | GPIO8  |

The display and the SD card **share one SPI bus**: SDA and SD_MOSI both go to
D10, SCK and SD_SCK both go to D8. Only the chip-select lines are separate.

```
 Display               XIAO ESP32-C3
 ───────               ─────────────
  VCC  ───────────────  3V3
  GND  ───────────────  GND
  CS   ───────────────  D1  (GPIO3)
  RESET───────────────  D3  (GPIO5)
  A0   ───────────────  D2  (GPIO4)
  SDA  ───────────────  D10 (GPIO10, MOSI)
  SCK  ───────────────  D8  (GPIO8,  SCK)
  LED  ───────────────  D4  (GPIO6)
  SD_CS ──────────────  D5  (GPIO7)
  SD_MOSI ────────────  D10 (GPIO10, shared with SDA)
  SD_MISO ────────────  D9  (GPIO9,  MISO)
  SD_SCK ─────────────  D8  (GPIO8,  shared with SCK)
```

### Notes

- **SDA is not I²C** — on these modules it's the SPI MOSI line.
- D9 (GPIO9) is also the XIAO's BOOT button / a strapping pin. It's the
  board's standard MISO pin and works fine for the SD card, but if the board
  ever boots into download mode with a card inserted, remove the card while
  resetting.
- **Logic is 3.3 V only.** Power VCC from 3V3. Some modules have an onboard
  regulator and a `J1` jumper that allow 5 V on VCC, but 3V3 always works.
- **Backlight (LED):** most modules have a series resistor on LED, so it can
  be driven straight from a GPIO (~20 mA) and dimmed with PWM. If yours has
  no resistor, or draws more current, add a ~47–100 Ω resistor, drive it
  through a small transistor, or just tie LED to 3V3 for always-on.
- D8 (GPIO8) is a strapping pin; it's the XIAO's default SPI SCK and is fine
  for this use. D6/D7 (UART TX/RX) and D0 remain free.
- Keep wires short. If the image is noisy, lower `TFT_SPI_HZ` in
  `src/main.cpp` (e.g. to 16 MHz), and `SD_SPI_HZ` for SD read errors.

## Build & upload

```sh
pio run -t upload
pio device monitor
```

## Troubleshooting

| Symptom                                   | Fix                                                    |
|-------------------------------------------|--------------------------------------------------------|
| White screen, nothing drawn               | Check CS/A0/RESET/SDA/SCK wiring; check VCC is 3V3      |
| Garbage pixels along an edge / offset     | Change `INITR_BLACKTAB` to `INITR_GREENTAB` or `INITR_REDTAB` in `main.cpp` |
| Red and blue swapped                      | Try a different `INITR_*` tab type                     |
| Backlight off                             | Check LED wiring, or tie LED to 3V3 to test            |
| Always "Insert SD card" with a card in    | Check SD_CS/SD_MISO wiring; format card FAT32; try lowering `SD_SPI_HZ` |
| "Use 24-bit BMP"                          | Re-save as 24-bit uncompressed BMP (not 16-bit, 8-bit or RLE) |
| "Use baseline JPEG"                       | It's a progressive JPEG; re-encode it with the ffmpeg JPEG command above |
| "Use MJPEG AVI"                           | Video codec isn't MJPEG; convert with the ffmpeg command above |
| Video plays slowly / choppy               | Lower resolution to the screen size, raise `-q:v`, or lower fps |
