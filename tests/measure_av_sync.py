"""Create a flash-and-tone source and measure sync in a recorded playback."""

import argparse
from array import array
import json
from math import cos, pi
from pathlib import Path
import statistics
import subprocess
import sys
from tempfile import TemporaryDirectory


MARKERS = (1, 3, 5, 7, 9)
TONE_HZ = 1000


def make_fixture(path, audio_delay=0):
    flashes = "+".join(f"between(t,{start},{start + 0.375})" for start in MARKERS)
    tones = "+".join(f"between(t,{start + audio_delay},{start + audio_delay + 0.2})" for start in MARKERS)
    subprocess.run([
        "ffmpeg", "-v", "error", "-n",
        "-f", "lavfi", "-i", f"color=black:size=320x180:rate=60:duration=11,"
        f"drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='{flashes}'",
        "-f", "lavfi", "-i", f"aevalsrc='0.65*sin(2*PI*{TONE_HZ}*t)*({tones})':s=48000:d=11",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-movflags", "+faststart", str(path),
    ], check=True)


def probe(path, selector, entries):
    return json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", selector, "-show_entries", entries,
        "-of", "json", str(path),
    ]))


def ffmpeg_decode(path, options):
    return subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path), *options, "-"])


def picture_edges(path):
    frames = probe(path, "v:0", "frame=best_effort_timestamp_time").get("frames", [])
    timestamps = [float(frame["best_effort_timestamp_time"]) for frame in frames]
    # Keep the central 40% of the recording so that the environment outside the panel contributes less.
    levels = ffmpeg_decode(path, ["-map", "0:v:0", "-vf",
        "crop=iw*0.4:ih*0.4:iw*0.3:ih*0.3,scale=1:1:flags=area,format=gray",
        "-fps_mode", "passthrough", "-f", "rawvideo"])
    if len(timestamps) < 2 or len(levels) != len(timestamps):
        raise ValueError("could not match decoded video frames to their timestamps")
    ordered = sorted(levels)
    low, high = ordered[len(ordered) // 3], ordered[len(ordered) * 9 // 10]
    if high - low < 35:
        raise ValueError("the center of the video does not show clear flashes")
    threshold = (low + high) / 2
    edges = [current for previous, value, current in zip(levels, levels[1:], timestamps[1:])
             if previous < threshold <= value]
    frame_ms = 1000 * statistics.median(b - a for a, b in zip(timestamps, timestamps[1:]))
    return edges, frame_ms


def tone_onsets(samples, rate):
    length = rate // 100
    coefficient = 2 * cos(2 * pi * TONE_HZ / rate)
    matches = []
    for offset in range(0, len(samples) - length + 1, length):
        prior = older = energy = 0
        for value in samples[offset:offset + length]:
            energy += value * value
            prior, older = value + coefficient * prior - older, prior
        narrowband = (prior * prior + older * older - coefficient * prior * older) / length
        matches.append(energy > length * 60**2 and narrowband > 0.20 * energy)
    return [i * length for i in range(1, len(matches) - 1)
            if not matches[i - 1] and matches[i] and matches[i + 1]]


def sound_edges(path):
    audio = probe(path, "a:0", "stream=sample_rate:frame=best_effort_timestamp_time,nb_samples")
    if not audio.get("streams") or not audio.get("frames"):
        raise ValueError("the recording has no usable audio track")
    rate = int(audio["streams"][0]["sample_rate"])
    if rate <= 2 * TONE_HZ:
        raise ValueError("the recorded sample rate is too low for the marker")
    raw = ffmpeg_decode(path, ["-map", "0:a:0", "-ac", "1", "-c:a", "pcm_s16le", "-f", "s16le"])
    samples = array("h", raw)
    if sys.byteorder != "little":
        samples.byteswap()

    onsets = iter(tone_onsets(samples, rate))
    onset = next(onsets, None)
    position = 0
    edges = []
    for frame in audio["frames"]:
        end = position + int(frame["nb_samples"])
        while onset is not None and onset < end:
            edges.append(float(frame["best_effort_timestamp_time"]) + (onset - position) / rate)
            onset = next(onsets, None)
        position = end
    if position != len(samples):
        raise ValueError("could not match decoded audio samples to their timestamps")
    return edges


def measure(path):
    flashes, frame_ms = picture_edges(path)
    tones = sound_edges(path)
    pairs = []
    j = 0
    for flash in flashes:
        while j < len(tones) and tones[j] < flash - 0.8:
            j += 1
        if j < len(tones) and abs(tones[j] - flash) <= 0.8:
            pairs.append(round((tones[j] - flash) * 1000, 1))
            j += 1
    if len(pairs) < 3:
        raise ValueError(f"need at least three matching flash-and-tone markers, found {len(pairs)}")
    return {
        "audio_minus_video_ms": pairs,
        "median_ms": statistics.median(pairs),
        "camera_frame_interval_ms": round(frame_ms, 1),
        "audio_window_ms": 10,
    }


def self_test():
    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        for name, milliseconds in (("aligned", 0), ("audio_late", 120), ("audio_early", -120)):
            source = root / f"{name}.mp4"
            make_fixture(source, milliseconds / 1000)
            reduced = root / f"{name}-30fps.mp4"
            subprocess.run(["ffmpeg", "-v", "error", "-i", str(source), "-vf", "fps=30",
                            "-c:v", "libx264", "-c:a", "copy", str(reduced)], check=True)
            for rate, path in ((60, source), (30, reduced)):
                report = measure(path)
                if len(report["audio_minus_video_ms"]) != len(MARKERS) or any(
                    abs(offset - milliseconds) > report["camera_frame_interval_ms"] + report["audio_window_ms"]
                    for offset in report["audio_minus_video_ms"]
                ):
                    raise ValueError(f"known offset check failed at {rate} fps: {report}")
                print(json.dumps({"case": name, "fps": rate, "input_offset_ms": milliseconds, **report}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="operation", required=True)
    subcommands.add_parser("make", help="create the source MP4").add_argument("path", type=Path)
    subcommands.add_parser("measure", help="analyze a recording with video and audio").add_argument("path", type=Path)
    subcommands.add_parser("self-test", help="check known offsets at 30 and 60 fps")
    args = parser.parse_args()
    try:
        if args.operation == "make":
            make_fixture(args.path)
        elif args.operation == "measure":
            print(json.dumps(measure(args.path), indent=2))
        else:
            self_test()
    except (KeyError, OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Sync check failed. {error}\n")


if __name__ == "__main__":
    main()
