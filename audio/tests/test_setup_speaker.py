import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from setup_speaker import command, set_up


class Link:
    def __init__(self, reply):
        self.replies = iter((b"boot noise\n", b"TVSET READY\r\n", reply))
        self.writes = []

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def readline(self):
        return next(self.replies)


wire = command("folo", "syntheti", "192.0.2.21", 18096)
assert wire == b"TVSET1 545653310408c000021546b0666f6c6f73796e7468657469\n"
assert command("next", "", "192.0.2.22", 18097) == b"TVSET1 545653310400c000021646b16e657874\n"
for invalid in (
    ("", "", "192.0.2.21", 1),
    ("硬" * 11, "", "192.0.2.21", 1),
    ("name", "short", "192.0.2.21", 1),
    ("name", "", "239.0.0.1", 1),
    ("name", "", "192.0.2.21", 0),
):
    try:
        command(*invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid setup settings were accepted")

link = Link(b"TVSET SAVED\n")
assert set_up(link, wire) is None
assert link.writes == [b"\nTVSET ?\n", b"\nTVSET ?\n", wire]
link = Link(b"TVSET STORE_FAILED\n")
try:
    set_up(link, wire)
except RuntimeError as error:
    assert "local storage" in str(error)
else:
    raise AssertionError("A failed commit was reported as saved")
assert link.writes.count(wire) == 1
print("USB host tool passed framing, bounds, acknowledgement and single-write failure handling.")
