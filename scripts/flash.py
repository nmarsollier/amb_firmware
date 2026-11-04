#!/usr/bin/env python3
"""Button-less flash: reboot the module into download mode, then flash it.

Flow:
  1. Confirm the USB CDC port is actually the AMB (`capabilities` → device_id "amb").
  2. Send `action/device/bootloader` over the CDC port.
  3. Wait for the USB-Serial-JTAG (download) port to enumerate.
  4. Run `idf.py -p <port> flash`.

A board that is already in ROM download mode has no AMB app answering
`capabilities`; flash it with `--download PORT` (or `PORT=... --download`).

The CDC port is taken from a command-line argument, the PORT environment
variable, or auto-detected (the /dev/cu.usbmodem* device whose name carries the
firmware's TinyUSB serial "000001").  Requires the ESP-IDF environment.
"""
import glob
import json
import os
import select
import struct
import subprocess
import sys
import termios
import time
import tty

BODY = (
    '{"proto":2,"type":"action","id":1,"session":"s",'
    '"target":"device","action":"bootloader"}'
)


def parse_args(argv):
    download = False
    port = None
    for a in argv[1:]:
        if a in ("--download", "-d"):
            download = True
        elif not a.startswith("-"):
            port = a
    return download, port


def list_ports():
    return set(glob.glob("/dev/cu.usbmodem*"))


def pick_cdc():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        return None
    # Prefer the app's TinyUSB serial string "000001".
    matching = [p for p in ports if "000001" in p]
    if len(matching) == 1:
        return matching[0]
    if len(ports) == 1:
        return ports[0]   # only one device — unambiguous
    # Multiple devices and none uniquely identifies the AMB: refuse to guess.
    return None


def open_raw(port):
    """Open a serial port in raw mode (byte stream, no tty line processing)."""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    return fd


def write_all(fd, data):
    while data:
        n = os.write(fd, data)
        if n <= 0:
            return False
        data = data[n:]
    return True


def read_frame(fd, timeout):
    """Read one length-prefixed frame (u32 LE length + JSON body) with a real
    timeout.  Returns the body bytes, or None on timeout/error."""
    deadline = time.time() + timeout
    buf = b""
    while len(buf) < 4:
        remain = deadline - time.time()
        if remain <= 0:
            return None
        r, _, _ = select.select([fd], [], [], remain)
        if not r:
            return None
        chunk = os.read(fd, 4 - len(buf))
        if not chunk:
            return None
        buf += chunk

    (body_len,) = struct.unpack("<I", buf[:4])
    while len(buf) < 4 + body_len:
        remain = deadline - time.time()
        if remain <= 0:
            return None
        r, _, _ = select.select([fd], [], [], remain)
        if not r:
            return None
        chunk = os.read(fd, 4 + body_len - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf[4:4 + body_len]


def verify_amb(port, timeout=2.0):
    """Send `capabilities` and confirm the device identifies as 'amb'."""
    body = b'{"proto":2,"type":"capabilities","id":1}'
    frame = struct.pack("<I", len(body)) + body
    try:
        fd = open_raw(port)
        try:
            if not write_all(fd, frame):
                return False
            resp = read_frame(fd, timeout)
        finally:
            os.close(fd)
        if resp is None:
            return False
        return json.loads(resp.decode("utf-8")).get("device_id") == "amb"
    except (OSError, ValueError):
        return False


def send_bootloader(port):
    body = BODY.encode("utf-8")
    frame = struct.pack("<I", len(body)) + body
    # Write-only open + drain before close: a raw O_RDWR open with an immediate
    # close could discard the frame before it reaches the device, so the module
    # would never reboot.  tcdrain() waits until the frame is transmitted.
    with open(port, "wb", buffering=0) as f:
        f.write(frame)
        termios.tcdrain(f.fileno())


def wait_for_new_port(before, timeout=12.0):
    """Return the set of ports that appeared after `before`; empty on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        new = list_ports() - before
        if new:
            return new
        time.sleep(0.25)
    return set()


def port_location(port):
    """Map a /dev/cu.* serial port to its USB locationID (macOS `ioreg`), or None.

    The locationID identifies the physical USB port and is stable across
    re-enumeration, so the AMB running the app and the same board in download
    mode share it.  Returns None when `ioreg` is unavailable or the port cannot
    be resolved.
    """
    try:
        out = subprocess.check_output(
            ["ioreg", "-r", "-c", "IOUSBHostDevice", "-l", "-w", "0"],
            timeout=15).decode("utf-8", "replace")
    except (OSError, subprocess.TimeoutExpired):
        return None

    location = None
    for line in out.splitlines():
        # ioreg tree lines are indented with spaces and '|' characters.
        field = line.lstrip(" |")
        if "+-o" in line and "IOUSBHostDevice" in line:
            location = None   # a new device block; its locationID follows
        elif field.startswith('"locationID"'):
            try:
                location = int(field.split("=", 1)[1].strip().rstrip(","))
            except ValueError:
                location = None
        elif field.startswith('"IOCalloutDevice"'):
            value = field.split("=", 1)[1].strip().strip('"')
            if value == port:
                return location
    return None


def main(argv):
    download, explicit_port = parse_args(argv)

    # Board already in ROM download mode: no AMB app, flash the port directly.
    if download:
        port = explicit_port or os.environ.get("PORT")
        if port is None:
            ports = sorted(glob.glob("/dev/cu.usbmodem*"))
            if len(ports) == 1:
                port = ports[0]
        if port is None:
            sys.stderr.write("--download requires an explicit PORT\n")
            return 1
        print("flashing (download mode) on %s" % port)
        return subprocess.call(["idf.py", "-p", port, "flash"])

    cdc = explicit_port or os.environ.get("PORT") or pick_cdc()
    if cdc is None:
        sys.stderr.write(
            "cannot unambiguously identify the AMB — pass PORT, or use "
            "--download if it is already in download mode\n")
        return 1

    if not verify_amb(cdc):
        sys.stderr.write(
            "%s does not answer as the AMB (capabilities). If it is already in "
            "download mode, use --download.\n" % cdc)
        return 1

    # Capture the AMB's USB location so the download port can be correlated to it
    # after the reboot (the same physical port keeps the same location).
    cdc_location = port_location(cdc)
    if cdc_location is None:
        sys.stderr.write(
            "cannot determine the AMB's USB location — aborting (flash manually "
            "with --download <port>)\n")
        return 1

    # Snapshot the ports present BEFORE the reboot so the download port can be
    # told apart from any other device that was already connected.
    before = list_ports()

    try:
        send_bootloader(cdc)
        print("bootloader command sent on %s" % cdc)
    except OSError:
        print("no app listening on %s" % cdc)

    new_ports = sorted(wait_for_new_port(before))
    if len(new_ports) != 1:
        shown = ", ".join(new_ports) or "none"
        sys.stderr.write("no unique download port appeared (got %s); aborting\n" % shown)
        return 1
    port = new_ports[0]

    # Correlate with the AMB's physical USB location: the download port must be
    # on the same USB location as the CDC port it replaced.  "CDC disappeared and
    # another port appeared" alone does not prove they are the same board.
    if cdc in list_ports():
        sys.stderr.write(
            "%s is still present — the new port may not be the AMB; aborting\n" % cdc)
        return 1

    new_location = port_location(port)
    if new_location is None or new_location != cdc_location:
        sys.stderr.write(
            "%s is not on the AMB's USB location (AMB=%s, port=%s); aborting — "
            "flash it manually with --download %s\n"
            % (port, cdc_location, new_location, port))
        return 1

    print("flashing on %s" % port)
    return subprocess.call(["idf.py", "-p", port, "flash"])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
