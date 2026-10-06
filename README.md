# mood-clip

A wearable hair clip that displays a mood. Change it with the onboard button,
or from a phone over the clip's own WiFi.

Hardware: LILYGO T-Display-S3 (ESP32-S3, 1.9" 170x320 ST7789) + 3.7V LiPo.

## Status

Running on hardware. Moods, phone control, custom messages and colour
picking all working. Battery untested — no cells yet.

## Controls

| Input | Action |
|---|---|
| BOOT button (GPIO0), short press | next mood |
| USER button (GPIO14), hold 2 seconds | toggle phone control |

## Phone control

Hold the USER button for two seconds. A bar appears at the bottom of the screen showing the network name. It disappears once your phone has loaded the page.

1. On your phone, join the WiFi network **mood-clip**
2. Open a browser and go to **192.168.4.1** (any address works — everything
   redirects to the page)
3. Tap a mood, type your own message and hit **set**, or pick a background colour with the swatch

No app to install, no account, and the clip never touches your home network.
The access point shuts itself off after three minutes of no activity, because
leaving the radio on flattens a small battery fast.

Your phone will warn that the network has no internet. That's expected — say
stay connected. If your phone keeps dropping back to mobile data, turn mobile
data off for a moment.

## Setup

Install:

- **esp32** boards package by Espressif (Boards Manager)
- **TFT_eSPI** by Bodmer (Library Manager)

Everything else — WiFi, WebServer, Preferences — ships with the ESP32 core.

TFT_eSPI ships the T-Display-S3 pin configuration but doesn't enable it. Open
`Documents/Arduino/libraries/TFT_eSPI/User_Setup_Select.h`, comment out
`#include <User_Setup.h>`, and uncomment
`#include <User_Setups/Setup206_LilyGo_T_Display_S3.h>`. Restart the IDE.

If Arduino ever offers to update TFT_eSPI, decline — upgrading overwrites that
file and you get a clean upload with a blank screen.

Board settings:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | **Enabled** |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |

## Customising

Moods live in the `MOODS[]` table near the top of the sketch. Each row is a
name, a face, a message, a background colour and a text colour. The name and
face appear on the phone buttons; the message is what scrolls across the clip
itself. Add or edit rows freely — the button cycling and the web page both
read the table.

Built-in fonts are ASCII only, so faces made from kana or emoji render as
blanks. Stick to keyboard characters.

## Notes

- The chosen mood and any custom message survive a reboot (saved to flash).
- Picking a preset clears a custom message.
- The AP is an open network by default, so anyone nearby could change the mood
  while it's up. It's only live for three minutes at a time. Set `AP_PASSWORD`
  to 8+ characters if that bothers you.
- **GPIO15 must be driven HIGH** or the screen stays black on battery. Already
  handled in `setup()`. It works fine over USB without it, which is how this
  wastes an evening.
- Expect roughly 3-4 hours on a 500mAh cell, less while the AP is running.
