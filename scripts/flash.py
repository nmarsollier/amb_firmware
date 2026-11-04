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


def list_ports():
    return set(glob.glob("/dev/cu.usbmodem*"))


def wait_for_new_port(before, timeout=12.0):
    """Return the set of ports that appeared after `before`; empty on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        new = list_ports() - before
        if new:
            return new
        time.sleep(0.25)
    return set()


def main(argv):
    cdc = pick_cdc(argv)
    if cdc is None:
        sys.stderr.write("no /dev/cu.usbmodem* port found\n")
        return 1

    # Snapshot the ports present BEFORE the reboot so the download port can be
    # told apart from any other device that was already connected.
    before = list_ports()

    try:
        send_bootloader(cdc)
        print("bootloader command sent on %s" % cdc)
    except OSError:
        print("no app listening on %s (already in download mode?)" % cdc)

    new_ports = sorted(wait_for_new_port(before))
    if len(new_ports) == 1:
        port = new_ports[0]
    elif len(new_ports) == 0:
        # No new port appeared — the chip may already be in download mode on
        # the same node. Fall back to it.
        port = cdc
    else:
        # More than one new port: don't guess — flashing the wrong board could
        # overwrite its firmware.
        sys.stderr.write("ambiguous: multiple new ports appeared (%s); aborting\n"
                         % ", ".join(new_ports))
        return 1

    print("flashing on %s" % port)
    return subprocess.call(["idf.py", "-p", port, "flash"])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
