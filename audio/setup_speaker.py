#!/usr/bin/env python3
"""Configure the FoloTV reSpeaker Lite through its native USB serial port."""

import argparse
from getpass import getpass
from ipaddress import IPv4Address
import struct
from time import monotonic


def command(ssid, password, sender, port):
    name = ssid.encode("utf-8")
    secret = password.encode("utf-8")
    addr = IPv4Address(sender).packed
    if not 1 <= len(name) <= 32 or b"\0" in name:
        raise ValueError("SSID must contain 1 to 32 UTF-8 bytes")
    if (secret and not 8 <= len(secret) <= 63) or b"\0" in secret:
        raise ValueError("Wi-Fi password must be 8 to 63 UTF-8 bytes or empty for an open network")
    if not 0 < addr[0] < 224 or addr[0] == 127 or not 1 <= port <= 65535:
        raise ValueError("Provide an Indicator unicast IPv4 address and a port from 1 to 65535")
    record = struct.pack("!4sBB4sH", b"TVS1", len(name), len(secret), addr, port) + name + secret
    return b"TVSET1 " + record.hex().encode("ascii") + b"\n"


def set_up(transport, wire):
    limit = monotonic() + 6
    while monotonic() < limit:
        transport.write(b"\nTVSET ?\n")
        if transport.readline().rstrip(b"\r\n") == b"TVSET READY":
            break
    else:
        raise RuntimeError("No setup reply from this USB port")

    if transport.write(wire) != len(wire):
        raise RuntimeError("Incomplete USB setup write")
    limit = monotonic() + 6
    while monotonic() < limit:
        reply = transport.readline().rstrip(b"\r\n")
        if reply == b"TVSET SAVED":
            return
        if reply == b"TVSET STORE_FAILED":
            raise RuntimeError("The board could not commit the settings to local storage")
        if reply == b"TVSET INVALID":
            raise RuntimeError("The board rejected the settings")
    raise RuntimeError("The board did not confirm the write. Settings may already have been saved.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="native USB serial port of the reSpeaker")
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--indicator", required=True, help="the Indicator's IPv4 address")
    parser.add_argument("--udp-port", type=int, default=18096)
    parser.add_argument("--open-network", action="store_true")
    args = parser.parse_args()
    try:
        import serial
        if not args.open_network:
            # getpass can fall back to echoing on a redirected stdin. Require a tty.
            import sys
            if not sys.stdin.isatty():
                parser.error("use an interactive terminal for the Wi-Fi password")
        password = "" if args.open_network else getpass("Wi-Fi password: ")
        payload = command(args.ssid, password, args.indicator, args.udp_port)
        with serial.Serial(args.port, 115200, timeout=0.35, write_timeout=1) as transport:
            set_up(transport, payload)
    except (ImportError, ValueError, RuntimeError, OSError) as error:
        parser.exit(1, f"Speaker setup failed. {error}\n")
    print("Settings saved. The reSpeaker is restarting.")


if __name__ == "__main__":
    main()
