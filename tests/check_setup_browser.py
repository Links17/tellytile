"""Drive the embedded setup page in a headless Chrome with a fixture HTTP server."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Thread
import argparse
import json
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/devices/sensecap-indicator/targets/examples/button_backlight/main/setup_portal.c"
SCRIPT = r"""
<script>
function assert(ok) { if (!ok) throw Error('portal assertion'); }
const until = async condition => {
    for (let i = 0; i < 75; ++i) {
        if (condition()) return;
        await new Promise(resolve => setTimeout(resolve, 50));
    }
    throw Error('portal timeout');
};
(async () => {
    await until(() => !saveButton.disabled);
    assert(fields.ssid.value === 'Studio "大屏" <script>');
    assert(fields.host.value === 'tv.local' && fields.audio_peer.value === '192.168.1.2');
    assert(!wifiKeep.hidden && !tokenKeep.hidden && !cancel.hidden);
    assert(!fields.password.value && !fields.token.value && !fields.code.value);
    f.requestSubmit();
    await until(() => !saveButton.disabled && out.textContent === 'first');
    scan.click();
    await until(() => !scan.disabled && scanStatus.textContent.startsWith('Could not scan'));
    assert(fields.ssid.value === 'Studio "大屏" <script>' && networkLabel.hidden);
    scan.click();
    await until(() => !scan.disabled && scanStatus.textContent.startsWith('No visible'));
    assert(networkLabel.hidden);
    scan.click();
    await until(() => !scan.disabled && !networkLabel.hidden);
    assert(networks.options.length === 3);
    assert(networks.options[1].textContent.includes('<script>') && !networks.querySelector('script'));
    networks.selectedIndex = 1;
    networks.dispatchEvent(new Event('change'));
    assert(!wifiKeep.hidden && fields.ssid.value === 'Studio "大屏" <script>');
    fields.password.value = 'drafted-for-studio';
    networks.selectedIndex = 2;
    networks.dispatchEvent(new Event('change'));
    assert(fields.ssid.value === 'guest' && !fields.password.value);
    fields.host.value = 'other.local';
    fields.host.dispatchEvent(new Event('input', {bubbles:true}));
    assert(wifiKeep.hidden && tokenKeep.hidden);
    f.requestSubmit();
    await until(() => !saveButton.disabled && out.textContent === 'second');
    cancel.click();
    await until(() => out.textContent === 'cancelled');
    document.body.dataset.result = 'passed';
})().catch(error => { document.body.dataset.result = error.message; });
</script>
"""


def run(browser):
    source = SOURCE.read_text()
    page = source.split("static const char page[] =", 1)[1].split("\n\nbool portal_config_read", 1)[0]
    html = "".join(json.loads(value) for value in re.findall(r'("(?:[^"\\]|\\.)*")', page))
    page = '<!doctype html><script>const setupToken = "fixture";</script>' + html + SCRIPT
    posts = []
    scan_requests = []

    class Server(BaseHTTPRequestHandler):
        def send_text(self, status, mime, body):
            self.send_response(status)
            self.send_header("Content-Type", f"{mime}; charset=utf-8")
            self.end_headers()
            self.wfile.write(body.encode("utf-8"))

        def do_GET(self):
            if self.path == "/":
                self.send_text(200, "text/html", page)
            elif self.path == "/saved" and self.headers.get("X-Setup-Token") == "fixture":
                self.send_text(200, "application/json", json.dumps({
                    "saved": True, "ssid": 'Studio "大屏" <script>', "host": "tv.local", "port": 10745,
                    "origin": "", "audio_peer": "192.168.1.2", "audio_port": 18096,
                    "has_password": True, "has_token": True,
                }))
            elif self.path == "/scan" and self.headers.get("X-Setup-Token") == "fixture":
                scan_requests.append(self.headers.get("X-Setup-Token"))
                if len(scan_requests) == 1:
                    self.send_text(500, "text/plain", "scan failed")
                else:
                    rows = [] if len(scan_requests) == 2 else [
                        {"ssid": 'Studio "大屏" <script>', "rssi": -48, "open": False},
                        {"ssid": "guest", "rssi": -62, "open": True},
                    ]
                    self.send_text(200, "application/json", json.dumps(rows))
            else:
                self.send_text(404, "text/plain", "")

        def do_POST(self):
            length = int(self.headers["Content-Length"])
            assert self.headers["X-Setup-Token"] == "fixture"
            posts.append((self.path, json.loads(self.rfile.read(length))))
            if self.path == "/cancel":
                self.send_text(200, "text/plain", "cancelled")
            else:
                self.send_text(400, "text/plain", "first" if len(posts) == 1 else "second")

        def log_message(self, *_):
            pass

    with TemporaryDirectory() as profile, ThreadingHTTPServer(("127.0.0.1", 0), Server) as server:
        worker = Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            page_dom = subprocess.run(
                [browser, "--headless", "--disable-gpu", "--no-first-run", f"--user-data-dir={profile}",
                 "--virtual-time-budget=8000", "--dump-dom", f"http://127.0.0.1:{server.server_port}/"],
                check=True, capture_output=True, text=True, timeout=30,
            ).stdout
        finally:
            server.shutdown()
            worker.join()
    assert 'data-result="passed"' in page_dom, re.findall(r'data-result="[^"]*"', page_dom)
    assert [path for path, _ in posts] == ["/configure", "/configure", "/cancel"]
    first, second, cancel = (values for _, values in posts)
    assert first["password"] is None and first["token"] is None and not first["code"]
    assert second["password"] == second["token"] == "" and second["ssid"] == "guest"
    assert not cancel
    assert len(scan_requests) == 3
    print("Chrome verified scan retry, empty results, safe SSID labels, password retention and clearing, setup saves and cancel.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("browser")
    run(parser.parse_args().browser)
