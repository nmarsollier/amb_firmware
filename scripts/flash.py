#!/usr/bin/env python3
"""Button-less flash: reboot the module into download mode, then flash it.

Flow:
  1. Send `action/device/bootloader` over the module's USB CDC port.
  2. Wait for the USB-Serial-JTAG (download) port to enumerate.
  3. Run `idf.py -p <port> flash`.

The CDC port is taken from a command-line argument, the PORT environment
variable, or auto-detected (the /dev/cu.usbmodem* device whose name carries the
firmware's TinyUSB serial "000001", falling back to the first such device).

Requires the ESP-IDF environment (`idf.py` on PATH); the `make flash` recipe
sources export.sh before invoking this script.
"""
import glob
import os
import struct
import subprocess
import sys
import time

BODY = (
    '{"proto":2,"type":"action","id":1,"session":"s",'
    '"target":"device","action":"bootloader"}'
)


def pick_cdc(argv):
    if len(argv) > 1 and argv[1]:
        return argv[1]
    if os.environ.get("PORT"):
        return os.environ["PORT"]
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        return None
    for p in ports:
        if "000001" in p:   # the app's TinyUSB serial string
            return p
    return ports[0]


def send_bootloader(port):
    body = BODY.encode("utf-8")
    frame = struct.pack("<I", len(body)) + body
    with open(port, "wb", buffering=0) as f:
        f.write(frame)


def wait_for_port(exclude, timeout=12.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        for p in ports:
            if p != exclude:
                return p
        time.sleep(0.25)
    return None


def main(argv):
    cdc = pick_cdc(argv)
    if cdc is None:
        sys.stderr.write("no /dev/cu.usbmodem* port found\n")
        return 1

    try:
        send_bootloader(cdc)
        print("bootloader command sent on %s" % cdc)
    except OSError:
        print("no app listening on %s (already in download mode?)" % cdc)

    port = wait_for_port(exclude=cdc)
    if port is None:
        # No new port appeared — the chip may already be in download mode on
        # the same node. Fall back to it.
        port = cdc

    print("flashing on %s" % port)
    return subprocess.call(["idf.py", "-p", port, "flash"])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
