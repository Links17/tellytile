"""Exercise FAV1 against the pinned original server with an original A/V fixture."""
from collections import Counter
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from urllib.error import HTTPError, URLError
from urllib.request import ProxyHandler, Request, build_opener

from receiver import Receiver
from vendor.fav1.protocol import ProtocolError

UPSTREAM = "30ba309d5fe77e86154de3ca1b81d457b65bc36f"
DEVICE = "021122334455"


class NativeComparison:
    def __init__(self, executable, quality):
        self.executable = executable
        self.quality = quality
        self.wire = bytearray()
        self.expected = []
        self.palette = None

    def record(self, packet):
        self.wire.extend(packet.encode())

    def decoded(self, packet):
        kind, pts, data, *extra = packet
        if kind == "video":
            if self.palette is None:
                self.palette = [
                    struct.pack("!H", round(r * 31 / 255) << 11 |
                                round(g * 63 / 255) << 5 | round(b * 31 / 255))
                    for r, g, b in extra[0]
                ]
            data = b"".join(self.palette[index] for index in data)
        self.expected.append(f"{kind} {pts} {zlib.crc32(data):08x}")

    def verify(self):
        actual = subprocess.check_output([self.executable, self.quality], input=self.wire, timeout=10)
        actual = actual.decode("ascii").splitlines()
        assert actual == self.expected, (len(actual), len(self.expected),
                                         next(((a, b) for a, b in zip(actual, self.expected) if a != b), None))
        counts = Counter(line.split()[0] for line in actual)
        print(f"Native C wire check ({self.quality}): {counts['video']} RGB565 frames and "
              f"{counts['audio']} PCM blocks matched reference checksums and timestamps.")


def check(upstream, recovery=False, rotate_ticket=False, native_check=None):
    if (upstream / ".git").exists():
        commit = subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
        if commit != UPSTREAM:
            raise ValueError(f"Expected original server commit {UPSTREAM}")
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary)
        source_available = threading.Event()
        source_available.set()
        subprocess.run([
            "ffmpeg", "-loglevel", "error", "-f", "lavfi", "-i",
            "testsrc2=size=320x180:rate=25", "-f", "lavfi", "-i",
            "sine=frequency=733:sample_rate=16000", "-t", "15",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac",
            "-movflags", "+faststart", str(path / "fixture.mp4"),
        ], check=True)
        if recovery:
            os.link(path / "fixture.mp4", path / "recovery.mp4")
        class QuietHandler(SimpleHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/recovery.mp4" and not source_available.is_set():
                    self.send_error(503)
                    return
                super().do_GET()

            def log_message(self, *_args):
                pass
        with ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=path)) as http:
            threading.Thread(target=http.serve_forever, daemon=True).start()
            try:
                check_server(upstream, path, http.server_port, source_available if recovery else None,
                             rotate_ticket, native_check)
            finally:
                http.shutdown()



def paired_ticket(port):
    credential = secrets.token_hex(32)
    opener = build_opener(ProxyHandler({}))
    def request(path, body, key=credential):
        req = Request(f"http://127.0.0.1:{port}/social/tv/{path}",
                      data=json.dumps(body).encode("ascii"),
                      headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
        with opener.open(req, timeout=2) as response:
            return json.load(response)

    deadline = time.monotonic() + 7
    while True:
        try:
            activated = request("activate", {"device": DEVICE, "code": "DEMO"})
            break
        except HTTPError:
            raise
        except URLError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.1)
    assert activated["host"] == "127.0.0.1"
    assert activated["port"] == port
    assert activated["ticket"].startswith(f"p1.{DEVICE}.")
    refreshed = request("config", {"device": DEVICE})
    assert refreshed == activated
    try:
        request("config", {"device": DEVICE}, secrets.token_hex(32))
    except HTTPError as error:
        assert error.code == 403
    else:
        raise AssertionError("Original service accepted a different device credential")
    print("Original service: enrollment and refresh passed, unrelated credential rejected.")
    return refreshed["ticket"], request


def stop_server(server):
    server.terminate()
    try:
        server.wait(timeout=5)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait()


def check_rotated_ticket(port, old_ticket, request):
    deadline = time.monotonic() + 7
    while True:
        try:
            refreshed = request("config", {"device": DEVICE})
            break
        except HTTPError:
            raise
        except URLError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.1)
    assert refreshed["ticket"] != old_ticket and refreshed["port"] == port
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.setblocking(False)
        try:
            Receiver(sock, "fixture", "standard", old_ticket)
        except (ProtocolError, ConnectionError, EOFError):
            pass
        else:
            raise AssertionError("Original service accepted the previous media ticket after rotation")
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.setblocking(False)
        client = Receiver(sock, "fixture", "standard", refreshed["ticket"])
        assert client.channel == "fixture"
        seen = set()
        for kind, *_ in client.packets():
            seen.add(kind)
            if {"video", "audio"} <= seen:
                break
        assert {"video", "audio"} <= seen
    print("Original service restart with a new signing token rejected the stale media ticket. "
          "Refreshing with the retained binding opened a new A/V session.")


def check_server(upstream, path, http_port, source_available, rotate_ticket, native_check):
    token = secrets.token_hex(32)
    for name, value in (("token", token), ("code", "DEMO")):
        secret = path / name
        secret.touch(mode=0o600)
        secret.write_text(value + "\n")
    channels = path / "channels.txt"
    names = {"fixture": "测试频道", "demo": "硬件演示"}
    if source_available is not None:
        names["recovery"] = "断流测试"
    channels.write_text("".join(
        f"{identifier} | {name} | http://127.0.0.1:{http_port}/"
        f"{'recovery' if identifier == 'recovery' else 'fixture'}.mp4\n"
        for identifier, name in names.items()
    ), encoding="utf-8")
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    env = os.environ | {"TV_CHANNELS_FILE": str(channels), "TV_DEGRADE": "ladder", "TV_GEOMETRY": "320x180"}
    log = path / "server.log"
    with log.open("w+") as output:
        def launch():
            return subprocess.Popen([
                sys.executable, "-m", "server.standalone", "--port", str(port),
                "--token-file", str(path / "token"), "--code-file", str(path / "code"),
            ], cwd=upstream / "service", env=env, stdout=output, stderr=output)

        server = launch()
        try:
            ticket, request = paired_ticket(port)
            for channel, quality in (("fixture", "smooth"), ("demo", "standard"), ("fixture", "ultra")):
                deadline = time.monotonic() + 7
                while True:
                    try:
                        sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                        break
                    except ConnectionRefusedError:
                        if time.monotonic() >= deadline or server.poll() is not None:
                            raise
                        time.sleep(0.05)
                with sock:
                    sock.setblocking(False)
                    native = NativeComparison(native_check, quality) if native_check else None
                    client = Receiver(sock, channel, quality, ticket, observe_packet=native.record if native else None)
                    assert client.channel == channel
                    assert {item["id"]: item["name"] for item in client.channel_list} == names
                    counts = Counter()
                    for packet in client.packets():
                        if native:
                            native.decoded(packet)
                        kind, _, data, *extra = packet
                        if kind == "video" and len(set(data)) < 3:
                            continue
                        if kind == "audio" and not any(data):
                            continue
                        counts[kind] += 1
                        if counts["video"] >= 2 and counts["audio"] >= 2:
                            break
                    assert counts["video"] >= 2 and counts["audio"] >= 2, (quality, counts)
                    print(f"{channel} / {quality}: decoded {counts['video']} full frames and "
                          f"{counts['audio']} audio blocks; channel names matched")
                    if native:
                        native.verify()
            if rotate_ticket:
                stop_server(server)
                (path / "token").write_text(secrets.token_hex(32) + "\n")
                server = launch()
                check_rotated_ticket(port, ticket, request)
            if source_available is not None:
                if rotate_ticket:
                    ticket = request("config", {"device": DEVICE})["ticket"]
                check_source_recovery(port, ticket, source_available, native_check)
        except BaseException:
            output.seek(0)
            print(output.read(), file=sys.stderr)
            raise
        finally:
            if server.poll() is None:
                stop_server(server)


def check_source_recovery(port, ticket, source_available, native_check):
    with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
        sock.setblocking(False)
        native = NativeComparison(native_check, "standard") if native_check else None
        client = Receiver(sock, "recovery", "standard", ticket, observe_packet=native.record if native else None)
        assert client.channel == "recovery"
        deadline = time.monotonic() + 38
        live = False
        snow_pts = []
        last_silence = -1
        recovered_picture = False
        for packet in client.packets():
            if native:
                native.decoded(packet)
            kind, pts, data, *_ = packet
            assert time.monotonic() < deadline, "source recovery did not complete before deadline"
            if kind == "video" and len(set(data)) > 2:
                if not live:
                    live = True
                    source_available.clear()  # The playing file reaches EOF, then reopening gets 503.
                elif source_available.is_set() and snow_pts:
                    recovered_picture = True
            elif kind == "video" and live and set(data) == {0, 255}:
                snow_pts.append(pts)
                if len(snow_pts) >= 3 and last_silence >= snow_pts[0]:
                    source_available.set()
            elif kind == "audio" and snow_pts:
                if not any(data):
                    last_silence = pts
                elif recovered_picture and pts >= snow_pts[-1]:
                    print(f"Original FAV1 session {client.session} recovered after {len(snow_pts)} snow frames "
                          "and silent audio blocks, then resumed colour and sound.")
                    if native:
                        native.verify()
                    return
        raise AssertionError("Original service ended the session before both picture and audio recovered")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("--recovery", action="store_true", help="exercise source loss and in-session recovery")
    parser.add_argument("--rotate-ticket", action="store_true", help="rotate the original server's signing token")
    parser.add_argument("--native-check", type=Path, help="compare against the native C FAV1 fixture")
    args = parser.parse_args()
    check(args.upstream.resolve(), args.recovery, args.rotate_ticket,
          args.native_check.resolve() if args.native_check else None)
