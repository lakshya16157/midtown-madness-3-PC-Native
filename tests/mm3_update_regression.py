"""Build the launcher's update module and run its contract against a local stand-in for GitHub."""
import hashlib
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = b"MZ" + bytes(range(256)) * 11718 + bytes(190)   # 3,000,000 bytes: a program-looking download


def release(base, tag, setup, checksum, with_sum=True):
    assets = [{"name": "MM3-Setup.exe", "size": len(setup), "browser_download_url": base + "/releases/download/MM3-Setup.exe"}]
    if with_sum:
        assets.append({"name": "MM3-Setup.exe.sha256", "size": 81,
                       "browser_download_url": base + "/releases/download/MM3-Setup.exe.sha256"})
    return json.dumps({"tag_name": tag, "body": "notes", "assets": assets}).encode()


def serve():
    files = {}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), None)

    def add(scenario, tag, setup=PROGRAM, checksum=None, with_sum=True, sum_text=None):
        base = f"http://127.0.0.1:{server.server_port}/{scenario}"
        digest = checksum or hashlib.sha256(setup).hexdigest()
        files[f"/{scenario}/latest"] = release(base, tag, setup, digest, with_sum)
        files[f"/{scenario}/releases/download/MM3-Setup.exe"] = setup
        files[f"/{scenario}/releases/download/MM3-Setup.exe.sha256"] = (sum_text or f"{digest}  MM3-Setup.exe\r\n").encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/slow/"):
                time.sleep(1.5)   # the launcher's Cancel during a check
            data = files.get(self.path)
            self.send_response(200 if data is not None else 404)
            self.send_header("Content-Length", str(len(data or b"")))
            self.end_headers()
            try:
                self.wfile.write(data or b"")
            except OSError:
                pass   # the client cancelled

        def log_message(self, *args):
            pass

    server.RequestHandlerClass = Handler
    add("same", "v2.1")
    add("newer", "v99.0")
    add("slow", "v99.0")
    add("nosum", "v99.0", with_sum=False)
    add("badsum", "v99.0", checksum="1" * 64)
    add("notexe", "v99.0", setup=b"<html>" + bytes(1000) + b"</html>")
    add("badsumfile", "v99.0", sum_text="not a checksum\n")
    add("short", "v99.0", setup=b"M")      # one byte: no program header after the hash matches
    files["/junk/latest"] = b"{not json"
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    server = serve()
    try:
        with tempfile.TemporaryDirectory(prefix="mm3-update-") as temporary:
            folder = Path(temporary)
            exe = folder / "update_contract.exe"
            subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
                            "/I" + str(ROOT / "src"), str(ROOT / "tests/mm3_update_contract.c"),
                            str(ROOT / "src/mm3_update.c"), "/Fe:" + str(exe), "/link", "winhttp.lib", "bcrypt.lib"],
                           check=True, cwd=folder)
            environment = {key: value for key, value in os.environ.items() if not key.lower().endswith("_proxy")}
            subprocess.run([str(exe), str(server.server_port), str(folder)], check=True, cwd=folder,
                           env=environment, timeout=120)
    finally:
        server.shutdown()


if __name__ == "__main__":
    main()
