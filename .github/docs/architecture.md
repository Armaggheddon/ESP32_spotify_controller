# Architecture

How the firmware is put together: the task model, the trip from Spotify to the
display, the memory rules that keep it stable, and where to start when extending it.

For setup and usage see the [README](../README.md). Authorization has its own page,
[`spotify_auth.md`](spotify_auth.md).

## Task model

`app_main()` sets everything up and then returns. All the ongoing work happens in
tasks created by the libraries.

| Task | Priority | Stack | Wakes up on | Job |
|------|----------|-------|-------------|-----|
| `now_playing` (`update_playback_state`) | 1 | 8192 | every `update_delay_ms`, 200 ms by default | Polls `/me/player`, parses the reply, fires callbacks |
| `get_new_token` | 2 | 4096 | `xRefreshTokenTimer`, roughly every 3500 s, plus once at startup | Swaps the refresh token for a fresh access token |
| `task_button_handler` | 5 | 2048 | the button event queue | Logs presses and dispatches actions |
| `spotify_action_*` | 4 | 4096 | on demand, one shot | Makes a single Web API call, then `vTaskDelete(NULL)` |

Three things about that arrangement are worth knowing.

Button actions run in their own task and exit, so the button task never waits on the
network. A slow request cannot swallow the presses that follow it.

`update_delay_ms` comes from `spotify_set_update_delay()`. The default is 200 ms and
that is what `main.c` asks for.

The access token reaches the polling task through a one-item queue
(`TOKEN_QUEUE_LEN`, `TOKEN_QUEUE_ITEM_SIZE`). The queue doubles as a gate: the first
request cannot go out before a token exists.

## Data flow

```
Spotify Web API
      │  GET /me/player  (every 200 ms)
      ▼
update_playback_state ──► parse_playback_state_response[_base]()
      │
      ├─ track changed ──► on_song_change(SpotifyInfo_t *)  ──► my_song_change()
      │                                                          │ download 64x64 JPEG
      │                                                          │ jpeg_decode_process()
      │                                                          └ st7789_display_push_scaled()
      │
      └─ play state changed ──► on_play_pause_change(uint8_t) ──► my_pp_change()
```

The parser only pulls out the fields the structs declare, and each reply is compared
against the previous one. That comparison is the point: callbacks fire on a real
change, not on every poll.

You choose how much to parse at init time:

| Mode | Struct | Pick it when |
|------|--------|--------------|
| `SPOTIFY_INFO` | `SpotifyInfo_t` | You want the whole track object: album, artists, images, device |
| `SPOTIFY_INFO_BASE` | `SpotifyInfoBase_t` | You need a handful of fields and would rather not churn memory on every poll |

Both are declared in `lib/spotify/spotify.h`.

## Token lifecycle

1. At startup `get_new_token` posts `grant_type=refresh_token` to
   `https://accounts.spotify.com/api/token`, authenticated with Basic auth built
   from the client id and secret.
2. The access token that comes back is formatted once into a `Bearer ...` header
   (`_spotify_bearer_auth`) and pushed onto the token queue.
3. `xRefreshTokenTimer` fires every `3600 - 100` seconds. That is deliberately just
   short of the one-hour token lifetime, so a valid token is always in hand.
4. If the refresh token has expired or been revoked, Spotify replies
   `{"error":"invalid_grant"}`. The firmware logs a message that names the helper
   script to re-run. Details in [`spotify_auth.md`](spotify_auth.md).

## Display pipeline

Album art is downloaded at Spotify's smallest size, 64x64, decoded to RGB565, and
upscaled to 240x240 on the way out to the panel. No full-screen framebuffer is ever
allocated, which is the whole trick.

| Buffer | Size | Lives in |
|--------|------|----------|
| Album art JPEG, as downloaded | 16 KB (`IMG_BUFF_SIZE`) | `src/main.c` |
| Decoded RGB565 | 8192 B (64x64x2) | `album_art_buffer` in `src/main.c` |
| One transmission band | 3840 B (240x8x2), `MALLOC_CAP_DMA` | allocated in `st7789_display_init()` |

`st7789_display_push_scaled(src, src_w, src_h)` walks the destination in bands of
`ST7789_DISPLAY_BAND_ROWS` rows, eight by default, nearest-neighbour samples the
source into the band buffer, and calls `esp_lcd_panel_draw_bitmap()` once per band.

Two details will bite you if you touch this code.

`esp_lcd_panel_draw_bitmap()` is asynchronous. The DMA engine reads your buffer
after the call returns, so the band buffer is only safe to refill once
`on_color_trans_done` fires. That callback releases a semaphore, and the next band
waits on it. Remove that handshake and you will see torn, corrupt bands, so if you
want to drop it, double-buffer instead.

A 3840-byte band sits just under the usual SPI DMA transfer limit of 4092 bytes,
so each band goes out in a single transfer. Bigger bands would need a different
approach.

Nearest-neighbour upscaling from 64x64 is blocky by nature. If that bothers you,
request a larger source image such as `SPOTIFY_ALBUM_ART_SIZE_300x300`. You trade
download size for sharpness, nothing else changes.

## Memory and boot order

The ESP32 has roughly 320 KB of RAM, but **DMA-capable** memory is a smaller pool
that Wi-Fi also draws from. That distinction causes the most confusing failures in
this project.

Large contiguous DMA allocations belong early in `app_main`, before `wifi_init()`.
Once Wi-Fi and lwIP are running, a big contiguous request can fail, and the failure
surfaces later as something unrelated, such as the JPEG decoder complaining that it
has no destination buffer.

The band buffer is only 3840 B, so the display no longer depends on a screen-sized
allocation at all. It is still initialized before Wi-Fi starts, so the DMA pool is
untouched when the request is made.

`api_response` inside the library is a single static 7000-byte buffer
(`SPOTIFY_MAX_API_LEN`) reused for every poll. It is global, not per-task, so do not
hand a pointer into it to another task and expect it to stay valid.

## Flash and partitions

The stock single-app partition table gives the application 1M, and this firmware
outgrew it. `partitions.csv` declares a 1500K application partition, and
`board_build.partitions` in `platformio.ini` selects it:

```
# Name,   Type, SubType, Offset,   Size,   Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  1500K,
```

Flipping `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE` in `sdkconfig.esp32dev` does not
work here. PlatformIO chooses the CSV itself, and it will happily report the large
table in the generated config while still emitting a 1M partition. To see what was
actually produced, decode the generated table:

```bash
uv run ~/.platformio/packages/framework-espidf/components/partition_table/gen_esp32part.py \
  .pio/build/esp32dev/partitions.bin
```

That prints the real layout, so it is the quickest way to confirm the 1500K factory
partition made it into the image.

## Extending

`src/main.c` is where app-level changes belong: pin choices, what each button does,
which callbacks get registered. The libraries stay generic and are driven from
there, so nearly everything below is an edit to `main.c`.

### Add a button

`BUTTON_ADD_TO_FAVOURITE_ID` is already in the enum, and `task_buttons_handler`
already dispatches it. It is just never registered, because only seven of the eight
ids are wired. Pick a free pin, then do two things in `app_main`:

```c
// with the other BUTTON_* defines
#define BUTTON_ADD_TO_FAVOURITE GPIO_NUM_27

// in app_main
buttons_init(8);   // was 7

buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE,
    BUTTON_INTR_EDGE, BUTTON_ADD_TO_FAVOURITE_ID, BUTTON_ADD_TO_FAVOURITE,
    0, 2, 0, gpio_interrupt_handler);
```

For the pin, stay off the `DISPLAY_*` pins, off GPIO 6 to 11 (the flash lines), and
off GPIO 34 to 39 (input only, no internal pull-up).

### React to playback state

```c
#include "spotify.h"

static void on_song(SpotifyInfo_t *info){
    ESP_LOGI(TAG, "Now playing: %s - %s",
        info->item->artists[0]->name, info->item->name);
}

static void on_play_pause(uint8_t is_playing){
    ESP_LOGI(TAG, "%s", is_playing ? "playing" : "paused");
}

void app_main(void){
    nvs_flash_init();
    wifi_init();

    spotify_init(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET,
                 SPOTIFY_REFRESH_TOKEN, SPOTIFY_INFO);
    spotify_add_on_song_change_cb(on_song);
    spotify_add_on_play_pause_change_cb(on_play_pause);
    spotify_set_update_delay(500);   // poll every 500 ms
    spotify_start();
}
```

Callbacks run on the polling task, so keep them quick. Anything that blocks there
delays the next update. Network calls and decoding belong on a task of your own.

### Playback actions

Every `spotify_action_*` function spawns a short-lived task and returns immediately,
so it is safe to call from any context, including a button handler. The complete
list, with notes on each, is the
[feature table in the README](../README.md#library-features). The example in
`src/main.c` maps one call to each button inside the `switch` in
`task_buttons_handler`.

### Modify the Web API client

`lib/spotify/spotify.c` wraps `esp_http_client`. One rule matters before you add a
request that parses a response body.

> **Never call `esp_http_client_read_response()` after
> `esp_http_client_perform()`.** `perform()` has already read and discarded the
> entire body, and ESP-IDF deliberately does not cache it in that case, so the call
> hands you zero bytes and your parse quietly returns nothing. Set an
> `event_handler` and collect `HTTP_EVENT_ON_DATA` instead.
> `HttpBodyCollector_t` and `http_body_collector_handler` in `spotify.c` exist for
> exactly this. Reset the collector *before* `perform()`, since `perform()`
> dispatches `ON_FINISH` and `DISCONNECTED` before it returns.

`esp_http_client_read_response()` is only valid on the manual path:
`esp_http_client_open()`, then `fetch_headers()`, then `read()`.
