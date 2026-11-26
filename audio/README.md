# Optional reSpeaker Lite audio

TellyTile can send audio to a reSpeaker Lite connected to a speaker. Put the reSpeaker and Indicator on the same network and use the same UDP port on both, normally `18096`.

1. Flash the [reSpeaker Lite companion firmware](release/link-recovery-respeaker/firmware.bin) using the XIAO ESP32-S3's USB port. This is the firmware for the audio companion.
2. With `pyserial` installed, configure the reSpeaker over USB with the [setup script](setup_speaker.py). Replace `PORT`, `Your Wi-Fi` and the example address with the reSpeaker serial port, Wi-Fi name and Indicator LAN address. The script prompts for the Wi-Fi password.

   ```sh
   python3 setup_speaker.py PORT --ssid 'Your Wi-Fi' --indicator 192.168.1.20
   ```

3. Add the reSpeaker's LAN address under **Speaker board IPv4 address** in the Indicator's setup page and save.

The companion source is in [respeaker](respeaker/). Host tests cover the relay and audio decoding. Two-board acoustic sync still needs a physical measurement.
