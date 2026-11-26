# GX fullscreen and live channel

The GX firmware scales a centered square crop of the 320×180 FAV1 picture to fill the 480×480 LCD. Playback starts with controls hidden. A tap opens the controls, while tapping the picture again or reaching the idle deadline restores the unobstructed view. The native host checks verify the crop edges, overlay row preservation, initial tap gating, swipe handling and dismissal deadlines.

The local test service offers DW English Live as the first channel, followed by the two existing test patterns. The live HLS playlist advanced from media sequence 299284239 to 299284241 during a seven-second interval. A FAV1 host client decoded distinct frames from `dw-live` before the playlist was deployed. The service retained the existing LAN endpoint and device pairing.

The GX app region was written and hash-verified on 2026-11-26. After restarting, the device rejoined its saved Wi-Fi, initialized the FT6336U controller and selected `dw-live / ultra` from three advertised channels. The Mac observed the established media connection and active transmission. The user confirmed on the physical device that the video filled the screen and that a tap brought up the controls.

[Host checks](fullscreen-gx-host.txt) · [Write](indicator-gx-fullscreen-write.txt) · [Boot and channel](indicator-gx-fullscreen-boot.txt)
