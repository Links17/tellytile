"""Host verification receiver for the upstream FAV1 wire format."""
import socket
import struct
import zlib

from vendor.fav1 import adpcm, frames
from vendor.fav1.protocol import (
    Kind, Packet, ProtocolError, json_bytes, json_object, receive_packet, send_packet,
)

QUALITIES = ("smooth", "standard", "ultra")
FAV1_FPS = (8, 15, 30)


class Receiver:
    def __init__(self, sock, channel, quality, token, *, observe_packet=None):
        self.sock = sock
        hello = {"version": 1, "channel": channel, "quality": quality}
        if token:
            hello["token"] = token
        send_packet(sock, Packet(Kind.HELLO, 0, 0, 0, json_bytes(hello)), timeout=2)
        reply = receive_packet(sock, 3)
        self.observe_packet = observe_packet
        if self.observe_packet:
            self.observe_packet(reply)
        if reply.kind != Kind.CONFIG or reply.seq or not reply.session:
            raise ProtocolError("TV service did not accept the session")
        config = json_object(reply.payload)
        expected = {"session": reply.session, "width": frames.WIDTH,
                    "height": frames.HEIGHT, "stripe_rows": frames.STRIPE_ROWS,
                    "sample_rate": 16000, "channels": 1, "sample_bits": 16,
                    "audio_chunk_ms": 40, "audio_codec": "ima_adpcm",
                    "quality": quality, "fps": FAV1_FPS[QUALITIES.index(quality)]}
        if any(config.get(key) != value for key, value in expected.items()):
            raise ProtocolError("Unsupported TV stream format or quality")
        self.channel = config.get("channel")
        self.channel_list = config.get("channel_list")
        self.session = reply.session
        self.seq = 1
        self.palette = None
        self.frame = bytearray(frames.FRAME_PIXELS)
        self.next_stripe = 0
        self.pending_pts = None

    def video(self, packet):
        data = packet.payload
        if self.palette is None or len(data) < 2:
            raise ProtocolError("Missing palette or short video packet")
        first, count = data[:2]
        end = 2 + 2 * count
        if not count or first + count > frames.STRIPES or end > len(data):
            raise ProtocolError("Invalid video stripe range")
        lengths = struct.unpack_from(f"!{count}H", data, 2)
        if sum(lengths) != len(data) - end:
            raise ProtocolError("Invalid video stripe lengths")
        if packet.flags:
            if self.pending_pts != packet.pts_ms or first != self.next_stripe:
                raise ProtocolError("Unexpected video continuation")
        elif first or self.pending_pts is not None:
            raise ProtocolError("Invalid start of a video frame")
        else:
            self.pending_pts = packet.pts_ms

        for index, length in enumerate(lengths, first):
            if length:
                decoder = zlib.decompressobj()
                try:
                    raw = decoder.decompress(data[end:end + length], frames.STRIPE_PIXELS + 1)
                except zlib.error as error:
                    raise ProtocolError("Invalid compressed stripe") from error
                if len(raw) != frames.STRIPE_PIXELS or not decoder.eof or decoder.unused_data:
                    raise ProtocolError("Invalid stripe pixels")
                offset = index * frames.STRIPE_PIXELS
                self.frame[offset:offset + frames.STRIPE_PIXELS] = raw
            end += length
        self.next_stripe = first + count
        if self.next_stripe != frames.STRIPES:
            return None
        self.pending_pts = None
        return ("video", packet.pts_ms, bytes(self.frame), self.palette)

    def packets(self):
        while True:
            packet = receive_packet(self.sock, 30 if self.palette is None else 4, self.session)
            if self.observe_packet:
                self.observe_packet(packet)
            if packet.seq != self.seq:
                raise ProtocolError("Unexpected packet sequence")
            self.seq = (self.seq + 1) & 0xffffffff
            if packet.kind == Kind.PALETTE and self.palette is None and packet.seq == 1:
                if len(packet.payload) != 512:
                    raise ProtocolError("Invalid RGB565 palette")
                self.palette = tuple(
                    ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
                    for c, in struct.iter_unpack("!H", packet.payload)
                )
            elif packet.kind == Kind.JPEG:
                frame = self.video(packet)
                if frame:
                    yield frame
            elif packet.kind == Kind.PCM and self.palette is not None:
                yield ("audio", packet.pts_ms, struct.pack("<640h", *adpcm.decode(packet.payload, 640)))
            elif packet.kind == Kind.END:
                return
            else:
                raise ProtocolError("Unexpected TV service packet")


