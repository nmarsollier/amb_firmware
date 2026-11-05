import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch, Mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flash


class FlashTests(unittest.TestCase):
    def test_ambiguous_or_missing_port_rejected(self):
        for ports in ([], ["a", "b"]):
            with patch.object(flash, "list_ports", return_value=ports):
                with self.assertRaises(flash.FlashError):
                    flash.pick_port()

    def test_explicit_port(self):
        self.assertEqual(flash.pick_port("chosen"), "chosen")

    def test_same_or_changed_port_requires_rom(self):
        for candidate in ("app", "rom"):
            with patch.object(flash, "list_ports", return_value=[candidate]), \
                 patch.object(flash, "probe_rom", return_value=(True, "")) as probe:
                self.assertEqual(flash.wait_for_rom("app"), candidate)
                self.assertEqual(probe.call_args.args[0], candidate)

    def test_multiple_reconnected_ports_rejected(self):
        with patch.object(flash, "list_ports", return_value=["a", "b"]), \
             patch.object(flash, "probe_rom") as probe:
            with self.assertRaises(flash.FlashError):
                flash.wait_for_rom("app")
            probe.assert_not_called()

    def test_existing_port_without_rom_does_not_succeed(self):
        with patch.object(flash, "list_ports", return_value=["app"]), \
             patch.object(flash, "probe_rom", return_value=(False, "not ROM")):
            with self.assertRaisesRegex(flash.FlashError, "ROM did not respond"):
                flash.wait_for_rom("app", timeout=0.01)

    def test_command_ack_and_rejection(self):
        for ok in (True, False):
            response = {"proto": 2, "type": "action", "id": 8, "session": "session",
                        "ok": ok, "data": {"status": "accepted"}, "error": "failed"}
            connection = Mock()
            connection.write.side_effect = len
            with patch.object(flash.serial, "Serial") as serial, \
                 patch.object(flash.secrets, "randbelow", return_value=7), \
                 patch.object(flash.secrets, "token_hex", return_value="session"), \
                 patch.object(flash, "read_frame", return_value=response):
                serial.return_value.__enter__.return_value = connection
                if ok:
                    flash.send_bootloader("app")
                else:
                    with self.assertRaisesRegex(flash.FlashError, "rejected"):
                        flash.send_bootloader("app")
                serial.assert_called_once()
                frame = connection.write.call_args.args[0]
                self.assertEqual(struct.unpack("<I", frame[:4])[0], len(frame) - 4)
                self.assertEqual(json.loads(frame[4:])["id"], 8)

    def test_oversized_frame_rejected(self):
        connection = Mock()
        connection.read.return_value = struct.pack("<I", 999999)
        with self.assertRaises(ValueError):
            flash.read_frame(connection, flash.time.monotonic() + 1)
        connection.read.assert_called_once_with(4)

    def run_flow(self, args, build_result=0, send_error=None, rom_error=None, flash_result=0):
        events = []
        def call(command):
            events.append(command[-1])
            return build_result if command[-1] == "build" else flash_result
        def send(port):
            events.append("send")
            if send_error:
                raise send_error
        def rom(port):
            events.append("rom")
            if rom_error:
                raise rom_error
            return "rom-port"
        with patch.object(flash.subprocess, "call", side_effect=call), \
             patch.object(flash, "pick_port", return_value="app"), \
             patch.object(flash, "send_bootloader", side_effect=send), \
             patch.object(flash, "wait_for_rom", side_effect=rom):
            result = flash.main(args)
        return result, events

    def test_build_failure_never_touches_device(self):
        self.assertEqual(self.run_flow([], build_result=2), (2, ["build"]))

    def test_lost_ack_or_already_rom_can_flash_after_handshake(self):
        self.assertEqual(self.run_flow([], send_error=TimeoutError()),
                         (0, ["build", "send", "rom", "flash"]))

    def test_rejection_never_flashes(self):
        self.assertEqual(self.run_flow([], send_error=flash.FlashError("rejected")),
                         (1, ["build", "send"]))

    def test_rom_failure_never_flashes(self):
        self.assertEqual(self.run_flow([], rom_error=flash.FlashError("no ROM")),
                         (1, ["build", "send", "rom"]))

    def test_download_skips_app_command_and_propagates_flash_failure(self):
        self.assertEqual(self.run_flow(["--download"], flash_result=3),
                         (3, ["build", "rom", "flash"]))

    def test_enter_only_never_builds_or_flashes(self):
        self.assertEqual(self.run_flow(["--enter-only"]), (0, ["send", "rom"]))


if __name__ == "__main__":
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        result = unittest.main(exit=False)
    print("%d tests; failures=%d errors=%d" %
          (result.result.testsRun, len(result.result.failures), len(result.result.errors)))
    for test, detail in result.result.failures + result.result.errors:
        print(test, detail)
    sys.exit(not result.result.wasSuccessful())
