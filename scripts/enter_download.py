#!/usr/bin/env python3
"""Put the TEMP module into ROM download mode for button-less flashing.

Sends the protocol's `action / device / bootloader` command over the module's
USB CDC port. The module answers `accepted` and, ~200 ms later, reboots into the
ROM bootloader (download mode), where its USB-Serial-JTAG port enumerates and
`idf.py flash` can program it without touching the BOOT button.

Port selection (first match wins):
  1. first command-line argument
  2. PORT environment variable
  3. the first /dev/cu.usbmodem* found on the system

Framing: the body is a JSON object preceded by its 4-byte little-endian length
(see main/proto/README.md).
"""
import glob
import os
import struct
import sys

BODY = (
    '{"proto":2,"type":"action","id":1,"session":"s",'
    '"target":"device","action":"bootloader"}'
)


def pick_port(argv):
    if len(argv) > 1 and argv[1]:
        return argv[1]
    if os.environ.get("PORT"):
        return os.environ["PORT"]
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def main(argv):
    port = pick_port(argv)
    if port is None:
        sys.stderr.write("no USB CDC port found; set PORT or pass it as an argument\n")
        return 1

    body = BODY.encode("utf-8")
    frame = struct.pack("<I", len(body)) + body
    try:
        with open(port, "wb", buffering=0) as f:
            f.write(frame)
    except OSError as exc:
        sys.stderr.write("failed to open %s: %s\n" % (port, exc))
        return 1

    print("bootloader command sent on %s — device rebooting into download mode" % port)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
