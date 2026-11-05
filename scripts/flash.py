#!/usr/bin/env python3
"""Build first, request download mode, confirm the ROM, then flash one ESP32-S3."""
import argparse
import glob
import json
import os
import secrets
import struct
import subprocess
import sys
import time

import serial


class FlashError(Exception):
    pass


def list_ports():
    return sorted(glob.glob("/dev/cu.usbmodem*"))


def pick_port(explicit=None):
    if explicit:
        return explicit
    ports = list_ports()
    if len(ports) != 1:
        raise FlashError("expected one USB serial port; found %s. Use PORT explicitly."
                         % (", ".join(ports) or "none"))
    return ports[0]


def read_frame(connection, deadline):
    def read_exact(size):
        data = bytearray()
        while len(data) < size and time.monotonic() < deadline:
            chunk = connection.read(size - len(data))
            data.extend(chunk)
        if len(data) != size:
            raise TimeoutError("no complete bootloader response")
        return bytes(data)

    size, = struct.unpack("<I", read_exact(4))
    if not 0 < size <= 2048:
        raise ValueError("invalid response frame length")
    return json.loads(read_exact(size))


def send_bootloader(port):
    request_id = secrets.randbelow(0xFFFFFFFF) + 1
    session = secrets.token_hex(8)
    body = json.dumps({"proto": 2, "type": "action", "id": request_id,
                       "session": session, "target": "device",
                       "action": "bootloader"}, separators=(",", ":")).encode()
    frame = struct.pack("<I", len(body)) + body
    # Keep DTR and the parser session stable until the response or USB disconnect.
    with serial.Serial(port, baudrate=115200, timeout=0.1,
                       write_timeout=2, exclusive=True) as connection:
        connection.reset_input_buffer()
        if connection.write(frame) != len(frame):
            raise FlashError("incomplete bootloader command write")
        deadline = time.monotonic() + 2
        while True:
            response = read_frame(connection, deadline)
            if not isinstance(response, dict):
                raise ValueError("invalid bootloader response")
            if response.get("id") != request_id or response.get("session") != session:
                continue
            if response.get("ok") is not True:
                raise FlashError("bootloader rejected: %s" % response.get("error", response))
            if (response.get("proto") != 2 or response.get("type") != "action"
                    or not isinstance(response.get("data"), dict)
                    or response["data"].get("status") != "accepted"):
                raise FlashError("unexpected bootloader response: %s" % response)
            print("bootloader command accepted", flush=True)
            return


def probe_rom(port, timeout):
    # No reset and no RAM stub: only synchronize with the ROM and read its MAC.
    result = subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
         "--before", "no-reset", "--after", "no-reset", "--no-stub",
         "--connect-attempts", "1", "read-mac"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout)
    return result.returncode == 0, result.stdout.strip()


def wait_for_rom(original, timeout=15):
    deadline = time.monotonic() + timeout
    last_error = "no USB serial port available"
    while time.monotonic() < deadline:
        ports = list_ports()
        if len(ports) > 1:
            raise FlashError("multiple USB serial ports appeared; refusing to guess")
        # An explicit path may use a different device naming scheme.
        port = ports[0] if ports else (original if os.path.exists(original) else None)
        if port:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                ready, last_error = probe_rom(port, min(4, remaining))
                if ready:
                    return port
            except subprocess.TimeoutExpired:
                last_error = "ROM synchronization timed out on %s" % port
        time.sleep(min(0.25, max(0, deadline - time.monotonic())))
    raise FlashError("ESP32-S3 ROM did not respond. Close serial clients, or enter "
                     "download mode with BOOT/RESET and run make flash-download.\n"
                     + last_error)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--download", "-d", action="store_true",
                      help="board is already in download mode")
    mode.add_argument("--enter-only", action="store_true",
                      help="enter and verify download mode without building/flashing")
    parser.add_argument("port", nargs="?", default=os.environ.get("PORT"))
    args = parser.parse_args(argv)
    try:
        if not args.enter_only:
            result = subprocess.call(["idf.py", "build"])
            if result:
                return result
        port = pick_port(args.port)
        if not args.download:
            print("requesting bootloader on %s" % port, flush=True)
            try:
                send_bootloader(port)
            except (serial.SerialException, OSError, TimeoutError, ValueError) as exc:
                # ROM may already be running, or USB may vanish before the ACK.
                # Only a successful ROM handshake permits flashing in that case.
                print("command response unavailable (%s); checking ROM" % exc, flush=True)
        port = wait_for_rom(port)
        print("ESP32-S3 ROM confirmed on %s" % port, flush=True)
        if args.enter_only:
            return 0
        result = subprocess.call(["idf.py", "-p", port, "flash"])
        if result == 0:
            print("flash completed", flush=True)
        return result
    except (FlashError, OSError) as exc:
        print("flash: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
