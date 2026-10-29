#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The web installer (web/installer/*.js) against tools/fm1_install.py and
tools/fm1_send.py, with no FM-1: both talk to the same simulated FM-1 and
every SysEx message the host sends is compared byte for byte.

  tools/installer_compare.py [PACKAGE.fwsc] [--stock FM-1_015.fwsc]

PACKAGE defaults to build/moondaw.fwsc. With --stock, the return to the
official V15 is run too. Needs node (the JS runs in web/installer/test/harness.mjs).

What is compared:
  - the package: identity, and the logical image the device reads (SHA-256)
  - whole installs (step 1, the loader, step 2, the restart): every message the
    host sends, the steps reported and the outcome, in normal and failing cases
  - parse_identity, parse_request, response on fixed and random packets
  - the song link: pack7 / unpack7, the messages, a --list and --get session
  - the ZIP the page saves songs in, read back by Python's zipfile
  - when build/sim/moondaw-fm1 exists: the song link against the simulator
    (the firmware's own app/link.c, over UDP), every song read by both
"""
import argparse
import contextlib
import hashlib
import io
import json
import os
import queue
import random
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as fi  # noqa: E402
import fm1_send as fs     # noqa: E402

HARNESS = ROOT / "web" / "installer" / "test" / "harness.mjs"
FAST = {"open": 0, "start": 0, "reply": 0, "loader": 0, "reboot": 0, "retry": 0.01, "poll": 0.02, "hs": 0.2,
        "idle_check": 0.3, "idle_write": 0.3, "wait_loader": 1.0, "wait_reboot": 1.0}   # harness.mjs: the same


# ------------------------------------------------------- simulated FM-1 ---

def identity_pkt(text):
    d = bytes([0x00, 0x59, 0x11, 0x1D, 0x00, 0x00]) + text.encode("latin-1").ljust(27, b"\0")[:27] + b"\0"
    return b"\xF0" + fi.pack7(d) + b"\xF7"


def request_pkt(fl, addr, n):
    u = bytearray([0x00, 0x59, 0x30, 0x08, 0x00, 0x00, fl]) + addr.to_bytes(4, "little") + n.to_bytes(3, "little")
    u.append(~sum(u[6:14]) & 0xFF)
    return b"\xF0" + fi.pack7(u) + b"\xF7"


class FakeDevice:
    """An FM-1 that answers the handshake, and after UPGRADE asks for the scenario's reads, then
    for the finish address; then it becomes the loader, or the restarted firmware.
    harness.mjs FakeDevice follows the same rules."""

    def __init__(self, s):
        self.s, self.phase, self.sent, self.links = s, s["start"], [], []
        self.reqs, self.i, self.finishing, self.finish, self.finish_addr, self.drop_at = [], 0, False, False, 0, -1

    def port(self):
        return {"app": self.s["app_port"], "loader": self.s["loader_port"], "back": self.s["back_port"]}.get(self.phase)

    def ident(self):
        return {"app": self.s["app_id"], "loader": self.s["loader_id"], "back": self.s["back_id"]}.get(self.phase)

    def emit(self, pkt):
        for link in self.links:
            if link.name == self.port() and not link.closed:
                link.q.put(pkt)

    def advance(self, to):
        self.phase = to

    def receive(self, pkt):
        self.sent.append(pkt.hex())
        if pkt == fi.HS_QUERY:
            if self.ident():
                self.emit(identity_pkt(self.ident()))
            return
        if pkt == fi.UPGRADE:
            st = self.s.get(self.phase)
            if not st:
                return
            self.reqs, self.finish = st["reqs"], st["finish"]
            self.finish_addr = fi.FINISH_CHECK if self.phase == "app" else fi.FINISH_WRITE
            self.drop_at = st.get("drop_at", -1)
            self.drop_at = -1 if self.drop_at is None else self.drop_at
            self.i, self.finishing = 0, False
            self.next()
            return
        if self.finishing:
            self.finishing = False
            self.advance("loader" if self.phase == "app" else "back")
            return
        self.i += 1
        self.next()

    def next(self):
        if self.i == self.drop_at:
            self.advance("gone")
            return
        if self.i < len(self.reqs):
            self.emit(request_pkt(*self.reqs[self.i]))
        elif self.finish:
            self.finishing = True
            self.emit(request_pkt(0, self.finish_addr, 8))


class FakeLink:
    def __init__(self, dev, name):
        self.dev, self.name, self.q, self.closed = dev, name, queue.Queue(), False
        dev.links.append(self)

    @property
    def lost(self):
        return self.dev.port() != self.name

    def send(self, pkt):
        if self.lost:
            return False
        self.dev.receive(bytes(pkt))
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.closed = True


class FakeBackend:
    def __init__(self, dev):
        self.dev = dev

    def input_names(self):
        return ([self.dev.port()] if self.dev.port() else []) + ["Some Synth"]

    output_names = input_names

    def open(self, i, o):
        if i == "Some Synth":
            self.dev.sent.append("PROBED-OTHER-GEAR")
            raise OSError("probed")
        return FakeLink(self.dev, i)


def run_python(s, image, product):
    dev = FakeDevice(s)
    up = fi.Updater(FakeBackend(dev))
    steps = []
    found = up.find()
    if not found:
        result = "error:notfound"
    else:
        try:
            result = "ok:" + up.install(found, image, s.get("product") or product, lambda k, a=None: steps.append([k, a]))
        except fi.InstallError as e:
            result = "error:" + e.code
    return {"result": result, "steps": steps, "sent": dev.sent}


# ------------------------------------------------------------- scenarios ---

def scenarios(n_image, rng, back_id):
    check = [(0, 0, 512), (0, 512, 512)] + [(rng.choice((0, 1)), a, rng.randint(1, 512))
                                             for a in (rng.randrange(0, n_image - 512) for _ in range(40))]
    write = [(rng.choice((0, 1)), a, min(512, n_image - a)) for a in range(0, n_image, 512)]
    write += [(0, a, rng.randint(1, 512)) for a in (rng.randrange(0, n_image - 512) for _ in range(10))]
    base = {"start": "app", "app_port": "USB Composite Device", "loader_port": "Felucca", "back_port": "Felucca",
            "app_id": "FM-1_015", "loader_id": "ota-FM-1_900", "back_id": back_id,
            "app": {"reqs": check, "finish": True}, "loader": {"reqs": write, "finish": True}}

    def var(name, **kw):
        s = json.loads(json.dumps(base))
        s["name"] = name
        for k, v in kw.items():
            if isinstance(v, dict):
                s[k].update(v)
            else:
                s[k] = v
        return s
    return [
        var("install"),
        var("install-over-moondaw", app_id="FM-1_900", app_port="Felucca"),
        var("finish-cut-off-update", start="loader"),
        var("no-fm1", start="gone"),
        var("wrong-model", app_id="XY-7_001"),
        var("stops-in-check", app={"finish": False}),
        var("bad-request", app={"reqs": check[:5] + [(0, n_image - 10, 512)]}),
        var("unplugged-in-write", loader={"drop_at": 100}),
        var("loader-never-appears", loader_port=None),
        var("never-comes-back", back_port=None),
        var("other-identity-after", back_id="FM-1_917"),
    ]


def units(rng):
    ids = ["FM-1_900", "FM-1_917", "FM-1_015", "FM-1_014", "ota-FM-1_900", "FM-1_8000014", "FM\x1b[2J-1_5", "noversion"]
    identities = [identity_pkt(t).hex() for t in ids]
    identities.append((b"\xF0" + fi.pack7(b"\x00\x59\x12" + bytes(31)) + b"\xF7").hex())     # other command
    requests = []
    for _ in range(200):
        p = bytearray(request_pkt(rng.randrange(256), rng.randrange(1 << 32), rng.randrange(1 << 24)))
        if rng.random() < 0.2:
            p[rng.randrange(1, len(p) - 1)] ^= 0x01                                            # damaged
        requests.append(bytes(p).hex())
    responses = [[rng.randrange(1 << 32), os.urandom(rng.choice((0, 1, 7, 8, 100, 511, 512))).hex(), rng.randrange(256)]
                 for _ in range(100)]
    md_data = [bytes(rng.randrange(256) for _ in range(n)).hex() for n in list(range(0, 30)) + [119, 120, 240, 241]]
    md_messages = [[5, ""], [6, "03"], [7, "02" + (1234).to_bytes(4, "little").hex()], [1, os.urandom(21).hex()]]
    return {"identities": identities, "requests": requests, "responses": responses,
            "md_data": md_data, "md_messages": md_messages}, ids


class FakeSongs:
    """MOONDAW's song link, for fm1_send.py: the same answers as harness.mjs's"""

    def __init__(self, songs):
        self.songs, self.sent, self.q = songs, [], []

    def send(self, body):
        self.sent.append(body.hex())
        cmd, raw = body[3], fs.unpack7(body[4:])
        if cmd == 5:
            ans = bytes([5, 0, 4, 0, 0x10, 0x6D, 0x04, 0x00])
        elif cmd == 6:
            s = self.songs[raw[0]] if raw[0] < len(self.songs) else None
            n = len(self.songs)
            ans = bytes([6, 0, n, 0, len(s[1]) & 0xFF, len(s[1]) >> 8, 0, 0]) + s[0].encode() if s else bytes([6, 9, n, 0, 0, 0, 0, 0])
        elif cmd == 7:
            off = int.from_bytes(raw[1:5], "little")
            piece = self.songs[raw[0]][1][off:off + 120]
            ans = bytes([7, 0, len(piece), 0]) + off.to_bytes(4, "little") + piece
        self.q.append(fs.SIG + bytes([0x7F]) + fs.pack7(ans))

    def read(self, timeout):
        return self.q.pop(0) if self.q else None


def compare_sim(sim, check):
    """The song link against the simulator: fm1_send.py's --list and --get, and the page's"""
    import time
    port = 19700 + os.getpid() % 80
    with tempfile.TemporaryDirectory() as d:
        flash = Path(d) / "sim.flash"
        demo = sim.parent / "demo.flash"
        if demo.exists():
            flash.write_bytes(demo.read_bytes())
        env = dict(os.environ, MOONDAW_FLASH=str(flash), MOONDAW_LINK_PORT=str(port),
                   SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        proc = subprocess.Popen([str(sim)], cwd=d, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            time.sleep(1.5)
            link = fs.Udp(port)
            with contextlib.redirect_stdout(io.StringIO()):
                st, ver, free, _ = fs.ask(link, fs.PING)
                have = fs.songs(link)
                got = [[name, fs.fetch(link, slot, name, size).hex()] for slot, name, size in have]
            res = Path(d) / "out.json"
            r = subprocess.run(["node", str(HARNESS), "--udp", str(port), str(res)], capture_output=True, text=True, timeout=120)
            if r.returncode:
                sys.exit(f"harness.mjs --udp failed:\n{r.stderr}")
            js = json.loads(res.read_text())
        finally:
            proc.terminate()
            proc.wait()
    ok = check("simulator: ping", [ver, free], [js["ping"]["version"], js["ping"]["free"]])
    ok &= check("simulator: list", [list(h) for h in have], js["list"])
    ok &= check("simulator: songs read", got, js["got"])
    if ok:
        print(f"simulator (app/link.c over UDP): link version {ver}, {len(have)} songs, "
              f"{sum(len(h) // 2 for _, h in got)} bytes read, the same by fm1_send.py and the page")


# ------------------------------------------------------------------ main ---

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", default=str(ROOT / "build" / "moondaw.fwsc"))
    ap.add_argument("--stock", help="the official FM-1_015.fwsc: compare the return to V15 too")
    a = ap.parse_args()
    fi.DELAY.update(FAST)
    rng = random.Random(1)
    failures = []

    def check(what, py, js):
        if py != js:
            failures.append(what)
            print(f"  DIFFERS  {what}")
            if isinstance(py, list) and isinstance(js, list):
                for i, (x, y) in enumerate(zip(py, js)):
                    if x != y:
                        print(f"           first difference at #{i}:\n             py {str(x)[:160]}\n             js {str(y)[:160]}")
                        break
                else:
                    print(f"           lengths: py {len(py)}, js {len(js)}")
            else:
                print(f"           py {str(py)[:200]}\n           js {str(js)[:200]}")
            return False
        return True

    runs = [(a.package, False, "FM-1_900")] + ([(a.stock, True, "FM-1_015")] if a.stock else [])
    songs = [("KIDS", bytes(rng.randrange(256) for _ in range(1000))), None, ("EMPTY", b""),
             ("DPEN-CHORUS", bytes(rng.randrange(256) for _ in range(250)))]
    for path, stock, back in runs:
        product, image = fi.load_package(path, False)
        print(f"{Path(path).name}: {product}, logical image {len(image)} bytes")
        scs = scenarios(len(image), rng, back)
        if stock:
            for s in scs:
                s["app_id"], s["app_port"] = ("FM-1_900", "Felucca") if s["app_id"] == "FM-1_015" else (s["app_id"], s["app_port"])
                s["back_port"] = s["back_port"] and "USB Composite Device"
                if s["back_id"] == "FM-1_917":
                    s["back_id"] = "FM-1_900"
        u, ids = units(rng)
        sc = {"package": str(path), "stock": stock, "scenarios": scs, **u,
              "songs": [[s[0], s[1].hex()] if s else None for s in songs]}
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(sc, f)
        res = f.name + ".out"
        try:
            r = subprocess.run(["node", str(HARNESS), f.name, res], capture_output=True, text=True, timeout=600)
            if r.returncode:
                sys.exit(f"harness.mjs failed:\n{r.stderr}")
            js = json.loads(Path(res).read_text())
        finally:
            for x in (f.name, res):
                with contextlib.suppress(OSError):
                    os.unlink(x)

        check("identity", product, js["product"])
        check("logical image", hashlib.sha256(image).hexdigest(), js["image_sha256"])
        total = 0
        for s in scs:
            py = run_python(s, image, product)
            j = js["scenarios"][s["name"]]
            ok = check(f"{s['name']}: outcome", py["result"], j["result"])
            ok &= check(f"{s['name']}: steps", py["steps"], j["steps"])
            ok &= check(f"{s['name']}: messages sent", py["sent"], j["sent"])
            n = len(py["sent"])
            total += n
            if ok:
                print(f"  same     {s['name']:<24} {py['result']:<20} {n} messages, {sum(len(x) // 2 for x in py['sent'])} bytes")
        print(f"  {total} messages compared byte for byte")

        check("parse_identity", [(fi.parse_identity(bytes.fromhex(h)) or None) and fi.parse_identity(bytes.fromhex(h)).text
                                 for h in u["identities"]], js["identities"])
        pr = [fi.parse_request(bytes.fromhex(h)) for h in u["requests"]]
        check("parse_request", [list(x) if x else None for x in pr], js["requests"])
        check("response", [fi.response(ad, bytes.fromhex(d), fl).hex() for ad, d, fl in u["responses"]], js["responses"])
        check("song link pack7", [fs.pack7(bytes.fromhex(h)).hex() for h in u["md_data"]], js["md_pack7"])
        check("song link unpack7", [fs.unpack7(fs.pack7(bytes.fromhex(h))).hex() for h in u["md_data"]], js["md_unpack7"])
        check("song link messages", [(fs.SIG + bytes([c]) + fs.pack7(bytes.fromhex(h))).hex() for c, h in u["md_messages"]],
              js["md_messages"])

        link = FakeSongs(songs)
        with contextlib.redirect_stdout(io.StringIO()):
            st, ver, free, _ = fs.ask(link, fs.PING)
            have = fs.songs(link)
            got = [[name, fs.fetch(link, slot, name, size).hex()] for slot, name, size in have]
        check("song link: ping", [ver, free], [js["songlink"]["ping"]["version"], js["songlink"]["ping"]["free"]])
        check("song link: list", [list(h) for h in have], js["songlink"]["list"])
        check("song link: songs read", got, js["songlink"]["got"])
        check("song link: messages sent", link.sent, js["songlink"]["sent"])
        z = zipfile.ZipFile(io.BytesIO(bytes.fromhex(js["zip"])))
        check("zip", [z.testzip(), [[i.filename, z.read(i).hex()] for i in z.infolist()]],
              [None, [[n + ".trk", h] for n, h in got]])
        print(f"  units: {len(u['identities'])} identities, {len(u['requests'])} requests, {len(u['responses'])} responses, "
              f"song link ({len(link.sent)} messages), zip")
    sim = ROOT / "build" / "sim" / "moondaw-fm1"
    if sim.exists():
        compare_sim(sim, check)
    else:
        print("simulator: not built (make sim), its song link not compared")
    if failures:
        sys.exit(f"{len(failures)} differences")
    print("all the same")


if __name__ == "__main__":
    main()
