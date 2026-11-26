# Source provenance

Passport TV at commit [30ba309](https://github.com/guolifu/ai-passport-tv/tree/30ba309d5fe77e86154de3ca1b81d457b65bc36f) provides the FAV1 parser, ADPCM decoder, pairing client and settings validation in the firmware's main/fav1 directory and the three modules in vendor/fav1. The original MIT licenses are retained alongside these files. The receiver maintains the original protocol and adds a native Indicator display adapter. The host-only verification client is in receiver.py.

The channel controls reuse that commit's MIT bitmap-text functions and the original Passport TV Bitmap glyph data under SIL OFL 1.1. The font data and OFL notice are under the native example's `main/ui/`. The menu layout and touch navigation are implemented for the Indicator. The touch BSP files are from the pinned CodeCraft hardware reference, with the existing FT5x06 and GT911 dependency versions.

The firmware also includes [cJSON v1.7.19](https://github.com/DaveGamble/cJSON/tree/v1.7.19), with its MIT license alongside the parser sources. The board BSP came from a CodeCraft export and is tracked by its project lock. The application uses no upstream artwork, commercial channels or credentials.

The Indicator settings page and screen layout are native adaptations. Their original ASCII font is reused from this workspace. The optional PCM output runs on the sibling ESP-IDF reSpeaker receiver.

The local transport-loss view follows the recovery behavior in the pinned upstream `service/server/overlay.py` and `service/server/reconnect.py`. The Indicator generates the 2×2 black-and-white dots locally and draws the message with its existing setup font. The pinned service continues to generate its own notice and silent audio for source loss within an active session.

The Indicator vendors the pinned `firmware/components/passport_tv/av_channel_policy.c` and its header unmodified in `main/fav1`. The native loop uses the original three-attempt and five-attempt limits. For the Indicator, a valid ADPCM block or a complete decoded frame marks a channel as previously playable. A failed TCP connection, Wi-Fi link change or user interruption clears the failure streak.

The wire comparison uses the firmware's FAV1 parser, CONFIG validator and picture assembler, plus the same ADPCM decoder source compiled into the reSpeaker receiver. The test builds that code on the host with zlib for stripe inflation. The ESP32 build uses the existing ROM miniz adapter. [Native wire harness](tests/check_native_wire.sh)

The Indicator maps the original UP and DOWN channel steps to upward and downward swipes that start inside the video. It preserves the channel-list page in memory and clamps the page if the service shrinks the catalog. The host regression covers gesture tracking, rejected contact, overlay isolation, wraparound and page retention. Touch response awaits GX and DX device verification. [Channel controls host checks](evidence/channel-touch-host.txt)

The playback status panel brings the pinned player’s status view to the Indicator. The Indicator adapter uses Wi-Fi RSSI, its local IPv4 address, video connection state and a recent reSpeaker clock sample. The video area remains owned by the display task. The setup shortcut ends the current playback session before restarting into the portal. [Status regression](evidence/status-detail-host.txt) · [Clock regression](evidence/status-detail-playout-host.txt)

The tuning screen follows the seven RGB565 color bars in the pinned player's `draw_color_bars`. The Indicator uses its existing English setup glyphs inside the video area. The display task distinguishes current-session pictures, initial and deliberate tuning, tuning retry, and local snow after a live transport failure. [Waiting view and playout checks](evidence/tuning-host.txt)

The channel caption follows the pinned player's CONFIG confirmation, five-second duration and placement near the video bottom. The Indicator starts its timer on the first scheduled picture and uses the channel ID to deduplicate reconnects, so distinct stations with identical display names still announce. [Caption regression](evidence/channel-caption-host.txt)

The transient volume indicator follows the pinned player’s 1.5-second lifetime and its percentage and bar. The Indicator uses the unused band immediately above the picture and hides the indicator while a menu is open. The host check verifies the lifetime, bar levels and clipping. [Volume notice checks](evidence/volume-notice-host.txt)

The setup page follows the original nearby-network selection flow. The Indicator scans as a station on request while continuing to serve its setup access point. The response uses the strongest scan result per SSID, and the browser inserts names as text. Browser and native-format checks cover retries, empty scans and credential retention with the same SSID. Radio behavior and real phone connections still need board verification. [Wi-Fi setup checks](evidence/wifi-scan-host.txt)

The channel list and quality menus use the pinned player’s 12-second menu timeout. The status page uses its 8-second timeout. Channel pagination refreshes its deadline. The Indicator keeps a deliberate pause open and ignores a touch that started before a timed menu closed. [Menu timing checks](evidence/menu-timeout-host.txt)

The pinned player waits two seconds for its configuration header, allows 30 seconds between headers while starting, and applies a five-second inactivity watchdog after media starts. The Indicator keeps the longer startup budget when the palette arrives before the first valid audio block or decoded picture. Packet bodies use the pinned three-second read budget. [Timeout policy regression](evidence/receive-deadline-host.txt)
