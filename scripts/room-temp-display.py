#!/usr/bin/env python3
"""Live wall display of every Lab 8 board advertising in the room.

Scans BLE with BlueZ over D-Bus and serves a self-refreshing page for the
projector. Lab 8 boards get a card with the die temperature decoded from
their manufacturer data; every other advertiser in range is listed below,
which doubles as proof that the scan is alive.

    ./scripts/room-temp-display.py
    firefox http://127.0.0.1:8080

Only advertisers under company ID 0xffff are decoded. To check that the
laptop's Bluetooth works at all, use "bluetoothctl scan on".

Use --demo 30 to fill the wall with fake boards and check how the layout
lands on the projector before the room shows up.
"""
import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import dbus
import dbus.mainloop.glib
from gi.repository import GLib

COMPANY_ID = 0xFFFF

boards = {}
others = {}
boards_lock = threading.Lock()


def decode(props, prefix):
    name = props.get("Name") or props.get("Alias") or ""
    if not str(name).startswith(prefix):
        return None

    payload = None
    for cid, data in (props.get("ManufacturerData") or {}).items():
        if int(cid) == COMPANY_ID:
            payload = bytes(data)
    if payload is None or len(payload) < 2:
        return None

    return {
        "name": str(name),
        "temp": int.from_bytes(payload[:2], "little", signed=True) / 100.0,
        "rssi": int(props["RSSI"]) if "RSSI" in props else None,
    }


def note_other(addr, props):
    """Everything the scan sees that is not one of our boards."""
    with boards_lock:
        if addr in boards:
            return
        row = others.setdefault(addr, {"addr": addr, "name": "", "rssi": None})
        name = props.get("Name") or props.get("Alias")
        if name:
            row["name"] = str(name)
        if "RSSI" in props:
            row["rssi"] = int(props["RSSI"])
        md = props.get("ManufacturerData")
        if md:
            row["company"] = min(int(c) for c in md.keys())
        row["seen"] = time.monotonic()


def remember(path, props, prefix):
    addr = path.rsplit("/", 1)[-1].replace("dev_", "").replace("_", ":")
    entry = decode(props, prefix)
    if entry is None:
        note_other(addr, props)

    if entry is None:
        # BlueZ only signals manufacturer data when the bytes change, and a
        # board sitting at a steady temperature never changes them. An RSSI
        # update still proves it is alive, so refresh what we already know.
        with boards_lock:
            known = boards.get(addr)
            if known is None:
                return
            if "RSSI" in props:
                known["rssi"] = int(props["RSSI"])
            known["seen"] = time.monotonic()
        return

    entry["addr"] = addr
    entry["seen"] = time.monotonic()
    with boards_lock:
        others.pop(addr, None)
        # PropertiesChanged carries only what changed, so anything missing from
        # this update has to be carried over instead of overwritten with blank.
        known = boards.get(addr)
        if known:
            if entry["rssi"] is None:
                entry["rssi"] = known["rssi"]
            if not entry["name"]:
                entry["name"] = known["name"]
        boards[addr] = entry


def seed_demo(count):
    """Fake boards, to check how the wall looks before the room fills up."""
    for i in range(count):
        addr = "DE:M0:00:00:%02X:%02X" % (i // 256, i % 256)
        boards[addr] = {
            "addr": addr,
            "name": "Espressif-%04x" % (0xc3d6 + i),
            "temp": 26.0 + (i % 9) * 0.37,
            "rssi": -45 - (i % 40),
            "seen": time.monotonic(),
            "demo": True,
        }


def snapshot():
    now = time.monotonic()
    with boards_lock:
        rows = list(boards.values())
        rest = list(others.values())
    rows.sort(key=lambda r: r["name"])
    rest.sort(key=lambda r: (r["rssi"] is None, -(r["rssi"] or -999)))
    return {
        "boards": [
            {"addr": r["addr"], "name": r["name"], "temp": r["temp"],
             "rssi": r["rssi"],
             "age": 0.0 if r.get("demo") else round(now - r["seen"], 1)}
            for r in rows
        ],
        "others": [
            {"addr": r["addr"], "name": r.get("name") or "",
             "rssi": r.get("rssi"),
             "company": ("0x%04x" % r["company"]) if "company" in r else "",
             "age": round(now - r["seen"], 1)}
            for r in rest
        ],
    }


PAGE = """<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<title>Temperatura da sala</title>
<style>
  :root { --bg:#fafafa; --ink:#23373b; --bar:#3b3b3b; --muted:#6b7b7f;
          --line:#e2e6e6; --cyan:#0e7490; --red:#b5342a; --green:#15803d; }
  * { box-sizing:border-box; }
  /* The board grid must never be pushed off a projector, so the page itself
     never scrolls and only the "others" list does. */
  body { margin:0; background:var(--bg); color:var(--ink); height:100vh;
         display:flex; flex-direction:column; overflow:hidden;
         font-family:"Fira Sans",system-ui,-apple-system,sans-serif; }
  header { background:var(--bar); color:#fff; padding:16px 30px; flex:none;
           display:flex; align-items:baseline; gap:20px; }
  header h1 { margin:0; font-size:24px; }
  header .sub { color:#b9c0c1; font-size:13px; }
  header .k { margin-left:auto; font-size:13px; color:#b9c0c1;
              font-family:"Fira Code",ui-monospace,monospace; }
  header .k b { color:#fff; font-size:22px; }
  h2 { font-size:12px; text-transform:uppercase; letter-spacing:.18em;
       color:var(--muted); margin:22px 30px 10px; font-weight:700; flex:none; }
  /* Six per row, growing a line at a time as boards show up. */
  main { padding:0 30px; display:grid; gap:14px; flex:none;
         grid-template-columns:repeat(6,1fr); }
  .card { border:2px solid var(--green); border-radius:12px; background:#fff;
          padding:10px 14px; text-align:center; }
  .card .n { font-family:"Fira Sans",system-ui,sans-serif; font-size:17px;
             font-weight:700; color:var(--ink); }
  .card .t { font-size:36px; font-weight:700; letter-spacing:-.02em;
             font-variant-numeric:tabular-nums; margin:1px 0; }
  .card .t span { font-size:19px; font-weight:400; color:var(--muted); }
  .card .m { font-size:11px; color:var(--muted);
             font-family:"Fira Code",ui-monospace,monospace; }
  .card.stale { opacity:.4; border-color:var(--line); }
  .restwrap { flex:1 1 auto; min-height:0; overflow-y:auto; padding-bottom:20px; }
  table { width:calc(100% - 60px); margin:0 30px; border-collapse:collapse;
          font-family:"Fira Code",ui-monospace,monospace; font-size:12.5px; }
  td { padding:5px 10px; border-bottom:1px solid var(--line); color:var(--muted); }
  td.a { color:var(--ink); }
  tr.stale td { opacity:.45; }
  .empty { color:var(--muted); margin:0 30px; font-size:16px; }
</style></head><body>
<header>
  <h1>Temperatura da sala</h1>
  <div class="sub" id="hint">procurando...</div>
  <div class="k">placas <b id="nb">0</b> &nbsp; outros no ar <b id="no">0</b></div>
</header>
<h2>Placas do Lab 8</h2>
<main id="grid"></main>
<p class="empty" id="empty">Nenhuma ainda. Grave o Lab 8 e espere alguns segundos.</p>
<h2>Outros dispositivos vistos no scan</h2>
<div class="restwrap"><table id="rest"></table></div>
<script>
async function tick() {
  let d;
  try { d = await (await fetch('data.json', {cache:'no-store'})).json(); }
  catch (e) { return; }
  nb.textContent = d.boards.length;
  no.textContent = d.others.length;
  empty.style.display = d.boards.length ? 'none' : '';
  hint.textContent = d.boards.length ? 'atualiza sozinho' : 'procurando...';
  grid.innerHTML = d.boards.map(r => `
    <div class="card${r.age > 10 ? ' stale' : ''}">
      <div class="n">${r.name || '(sem nome)'}</div>
      <div class="t">${r.temp.toFixed(2)}<span> C</span></div>
      <div class="m">${r.addr} &middot; ${r.rssi ?? '--'} dBm &middot; ha ${r.age.toFixed(0)}s</div>
    </div>`).join('');
  rest.innerHTML = d.others.map(r => `
    <tr class="${r.age > 30 ? 'stale' : ''}">
      <td class="a">${r.addr}</td><td>${r.name || '(sem nome)'}</td>
      <td>${r.company || ''}</td><td>${r.rssi ?? '--'} dBm</td>
      <td>ha ${r.age.toFixed(0)}s</td>
    </tr>`).join('');
}
tick(); setInterval(tick, 1000);
</script></body></html>
"""



class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/data.json"):
            body = json.dumps(snapshot()).encode()
            ctype = "application/json"
        else:
            body = PAGE.encode()
            ctype = "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def scan(adapter_name, prefix):
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    path = "/org/bluez/" + adapter_name
    adapter = dbus.Interface(bus.get_object("org.bluez", path), "org.bluez.Adapter1")
    manager = dbus.Interface(bus.get_object("org.bluez", "/"),
                             "org.freedesktop.DBus.ObjectManager")

    for obj_path, ifaces in manager.GetManagedObjects().items():
        if "org.bluez.Device1" in ifaces:
            remember(obj_path, ifaces["org.bluez.Device1"], prefix)

    bus.add_signal_receiver(
        lambda p, i: "org.bluez.Device1" in i and remember(p, i["org.bluez.Device1"], prefix),
        dbus_interface="org.freedesktop.DBus.ObjectManager",
        signal_name="InterfacesAdded")
    bus.add_signal_receiver(
        lambda iface, changed, inval, path=None:
            iface == "org.bluez.Device1" and remember(path, changed, prefix),
        dbus_interface="org.freedesktop.DBus.Properties",
        signal_name="PropertiesChanged", arg0="org.bluez.Device1",
        path_keyword="path")

    adapter.SetDiscoveryFilter({"Transport": "le",
                                "DuplicateData": dbus.Boolean(True)})
    adapter.StartDiscovery()

    loop = GLib.MainLoop()
    try:
        loop.run()
    finally:
        try:
            adapter.StopDiscovery()
        except dbus.DBusException:
            pass


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--adapter", default="hci0")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--demo", type=int, default=0, metavar="N",
                    help="seed N fake boards to check the projected layout")
    ap.add_argument("--prefix", default="",
                    help="optional name filter; the company ID already identifies us")
    args = ap.parse_args()

    if args.demo:
        seed_demo(args.demo)

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print("display: http://127.0.0.1:%d  (adaptador %s, prefixo %r)"
          % (args.port, args.adapter, args.prefix))

    try:
        scan(args.adapter, args.prefix)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
