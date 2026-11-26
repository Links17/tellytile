# TellyTile setup and development notes

[Project overview](README.md) · [简体中文](README.zh_CN.md) · [Software sources and notices](UPSTREAM.md)

The ESP32-S3 firmware connects to the compatible streaming service over Wi-Fi and displays complete 320×180 frames. The device uses the upstream FAV1 parser, RGB565 palette and stripe codec. It can relay the original ADPCM packets to a reSpeaker Lite on the same network. The reSpeaker runs ESP-IDF and decodes samples for its speaker. [Audio receiver setup](audio/README.md)

## Flash a prebuilt release

Choose the GX or DX `firmware.bin` from the table below, then connect the Indicator with a USB data cable. With esptool on your computer, replace `PORT` with the Indicator's ESP32-S3 serial port and run this command from the directory containing the image. The write replaces the existing firmware.

```sh
python3 -m esptool --chip esp32s3 --port PORT write-flash 0x0 firmware.bin
```

## Set up playback

On first startup, or when the case button is held during startup, the receiver hosts a setup access point. Join the on-screen network and open `192.168.4.1` in a browser. The GX release uses an open setup network and holds the RP2040 in reset to silence the built-in buzzer. The RP2040 sensor and microSD functions are unavailable while this release runs. The DX release listed below uses the password shown on the display. Enter Wi-Fi and the original service location. Enter the pairing origin and four-character code to enroll with the upstream protocol. The receiver clears the saved code after enrollment and keeps the device binding. Each start, channel change, quality change or reconnect uses that binding to obtain the current playback endpoint and ticket. A manual service token is an alternative. To enable audio, enter the reSpeaker IPv4 address and use the same UDP port on both devices, default 18096. Saving restarts the receiver. The receiver stores the setup and pairing state in local NVS.

The GX firmware scales the center of the source to fill the 480×480 panel. Tap once to reveal the channel, playback and status controls. Tap the video again or wait six seconds to hide the basic controls. Channel, quality and status menus use their own timers. The DX release retains its original layout.

Choose **Scan Wi-Fi** on the setup page to list nearby 2.4 GHz networks by signal strength. The list labels open and secured networks. Selecting a network fills its name. The name can also be entered manually for a hidden network or after a scan failure. [Setup-page checks](evidence/wifi-scan-host.txt)

The setup page pre-fills the saved network name, service address and speaker address. Password, manual token and pairing code inputs start blank. Leave the corresponding checkbox selected to reuse a saved credential for the same Wi-Fi network or manual service endpoint. Clear the checkbox and leave the input blank to remove it. Choose Restart without changes to leave the saved configuration intact.

Tap the channel bar to browse and select a channel by name. The list reopens on the last page you browsed. Swipe up from the video to go to the next channel, or down to go to the previous channel. Both directions wrap around. The channel and quality menus and pause screen ignore channel swipes. Tap the frame-rate label to select Smooth at 8 fps, Standard at 15 fps or Ultra at 30 fps. The Indicator starts in Ultra and restores your last selection on subsequent boots. The label shows AUTO when an older service does not advertise a profile. The player accepts integer frame rates from 1 to 30 fps, schedules frames by presentation timestamp and shows the measured display frame rate in the status bar.

On GX, the basic controls close after six seconds of inactivity. The channel and quality menus close after 12 seconds of inactivity. The status page closes after 8 seconds. Turning a channel-list page restarts its timer. Playback continues while a menu closes, and the pause page stays open. A touch begun before automatic dismissal cannot activate the newly exposed controls. [Menu timing checks](evidence/menu-timeout-host.txt)

Close the menu to adjust brightness and volume in 10% steps while playback continues. The volume control appears when a speaker is configured and requires the companion firmware linked below. The Indicator saves both settings. The reSpeaker BOOT button still changes its current volume locally. The Indicator case button advances channels on a tap, changes quality on a hold of 0.6 seconds up to 1.6 seconds and cycles brightness on a hold of 1.6 seconds up to 3 seconds. A channel or profile change ends the current session and reconnects.

After a volume tap, the screen shows the percentage and a level bar above the video for 1.5 seconds. The channel caption stays visible at the bottom of the picture during its own display interval. [Volume notice checks](evidence/volume-notice-host.txt)

Tap 暂停 (Pause) or release the case button after holding it for at least 3 seconds to end the current playback session, including reSpeaker audio. Automatic reconnects stop while paused. Tap 继续播放 (Resume) or tap the case button to reconnect to the same live channel. Tap 网络与电视设置 (Network and TV Settings) or release the case button after holding it for at least 0.6 seconds to restart into the setup access point. For the same pairing origin, leave the code blank to reuse the saved binding.

The Indicator schedules frames by FAV1 timestamps on a dedicated display task. For standalone playback it starts from the first audio packet or complete frame with an 80 ms buffer. When paired, it samples the companion reSpeaker's block schedule and corrects deadlines for queued frames. Replies must match the pending probe and current session and arrive within 40 ms. The reSpeaker plays 40 ms blocks, inserts silence for missing blocks and drains buffered audio at a normal end. Physical A/V offset still needs measurement.

The player waits two seconds for the configuration header. After configuration it allows up to 30 seconds between headers until the first valid media arrives. An early palette keeps this startup budget in effect. After valid media starts arriving, five seconds without a new header triggers recovery. Packet bodies retain a separate three-second deadline. [Receive deadline checks](evidence/receive-deadline-host.txt)

At startup or on a channel change, the Indicator shows the original player's seven-color test pattern with an English status label. If that attempt fails before the first picture, the test pattern stays on screen with a retry message. If Wi-Fi or the service connection fails during playback, the Indicator shows local 4 fps snow with a `RECONNECTING...` message. A frame from the current session clears the notice once it reaches the display. If the channel source stops within a live FAV1 session, the original service still supplies the snow and silent audio.

When the service confirms a new channel, its name appears along the bottom of the playing video for about five seconds, starting with the first scheduled frame. Reconnecting to the same channel leaves the original timer in place. Opening the channel, profile, pause or status menu hides the caption on subsequent frames. [Channel caption checks](evidence/channel-caption-host.txt)

Both boards discard their old sockets when Wi-Fi reconnects or their local IPv4 address changes. The next stream starts with a new audio relay socket, and the reSpeaker resumes listening on the configured port. Keep the configured peer addresses current.

The Indicator follows the original channel retry rules. Once connected to the server, an unproven channel advances after three attempts without valid media, or five attempts for a channel picked by the viewer. A recently proven channel stays selected and reconnects. Wi-Fi and server connection failures also keep the current channel.

A case-button tap keeps the active session when the channel list is empty or contains only the current channel. Selecting the current channel or profile also keeps playback running. [Playback control checks](evidence/stable-controls-host.txt)

Choose the image for the confirmed panel assembly. The GX GuanXian ST7701S display uses SPI and RGB initialization. The DX DaXian display uses RGB initialization. D1 family product names alone cannot establish the panel assembly. Fullscreen playback and tap-to-show controls were observed on a GX test unit. Dual-board audio synchronization and the DX layout still need physical verification.

The status band above the picture updates Wi-Fi signal strength, frames successfully submitted to the panel and FAV1 receive rate about once per second. A channel switch or stream loss clears the old rates. `USB 5V` displays the Indicator's rated power source. The board has no built-in battery. [Seeed power specifications](https://wiki.seeedstudio.com/Sensor/SenseCAP/SenseCAP_Indicator/Get_started_with_SenseCAP_Indicator/)

Tap the status band to open playback details while the picture continues. The panel shows Wi-Fi RSSI, the Indicator IPv4 address, video connection state and the configured speaker clock state. Synced means the current session accepted a clock reply in the last three seconds. Tap the return control to close the panel or Network and TV Settings to open the setup hotspot.

The GX source is in `firmware/`. The DX source is available as the locked archive listed below. Activate ESP-IDF 6.0.2 in the corresponding source directory and run `python3 codecraft.py verify-project .` and `python3 codecraft.py build-project .`. The default image uses device setup. Optional ESP-IDF menuconfig values provide build-time inputs for development.

The host test starts the pinned original service, enrolls and refreshes the device, and checks generated video and audio under all three profiles and through source loss. Run `sh tests/check_playout.sh` for independent clocks, lost first audio, late replies, receiver clock recovery, reconnect frames, reordering and tail draining. [Firmware host checks](evidence/session-auth-host.txt) · [Setup page Chrome check](evidence/setup-edit-browser.txt) · [Original service recovery check](evidence/reconnect-upstream.txt) · [Original service ticket rotation](evidence/session-auth-upstream.txt)

With the original server's Python dependencies active, run `sh tests/check_native_wire.sh /path/to/streaming-service --recovery --rotate-ticket` to feed the original service's output into the native C protocol validator, picture assembler, and reSpeaker ADPCM decoder. The host uses zlib for stripe inflation and the ESP32 firmware uses ROM miniz. The check compares RGB565 and PCM checksums and timestamps. [End-to-end host check](evidence/receive-deadline-wire-host.txt)

| Assembly | Firmware and evidence |
| --- | --- |
| Indicator GX | [Firmware](release-fullscreen-gx/firmware.bin) · [Build](evidence/fullscreen-gx-build.json) · [Image verification](evidence/fullscreen-gx-image.json) |
| Indicator DX | [Firmware](release-receive-deadline-dx/firmware.bin) · [Locked source archive](tellytile-dx-source.zip) · [Build](evidence/receive-deadline-dx-build.json) · [Image verification](evidence/receive-deadline-dx-image.json) |
| reSpeaker Lite TV audio receiver | [Firmware](audio/release/link-recovery-respeaker/firmware.bin) · [Image verification](audio/evidence/link-recovery-image.json) |

Both panel builds use the same streaming protocol. Their build records verify the respective native display configurations. [Panel source record](firmware/devices/sensecap-indicator/AGENTS.md#display-assembly-provenance) · [Channel recovery and playout checks](evidence/channel-caption-host.txt) · [Original service profile checks](evidence/profile-upstream.txt)

The TV audio receiver and the #27 music and radio player use separate reSpeaker firmware images. Connect the receiver's native XIAO USB port and run `setup_speaker.py` to set Wi-Fi, the Indicator address and the audio port. The board restarts after saving and loads the settings from NVS. Run the tool again to update the settings. [Companion setup](audio/README.md) · [USB setup checks](audio/evidence/setup-respeaker-host.txt)

## Measure playback sync on the two boards

Use FFmpeg to generate a clip with white flashes and 1 kHz audio markers. Add the MP4 as a test channel on the original server, with the Indicator displaying video and its reSpeaker companion playing audio. Record the screen and speaker with one camera. Center the video area and retain at least three complete flash-and-tone markers. Record steady playback and playback after recovery in separate clips.

```sh
python3 tests/measure_av_sync.py make /tmp/folotv-sync.mp4
python3 tests/measure_av_sync.py measure /path/to/recorded-playback.mp4
```

A positive audio-minus-video result means audio is late. The camera itself contributes timing error. The report includes its median frame interval and the 10 ms tone analysis window. The host self-test covers zero and ±120 ms offsets at 30 and 60 fps. The original Standard stream preserved all five matched timestamps. The boards still need a recorded measurement. [Measurement self-test](evidence/av-sync-measurement-host.txt) · [Original server marker check](evidence/av-sync-source-upstream.txt)
