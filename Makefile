# temp_firmware — top-level convenience Makefile.
#
#   make flash         flash the firmware (button-less: enters download mode first)
#   make enter-flash   only put the module into ROM download mode
#
# `flash` sends the protocol's `action/device/bootloader` command over the USB
# CDC port, waits for the USB-Serial-JTAG (download) port to enumerate, then
# runs idf.py against it — no BOOT button needed. `enter-flash` only performs
# the first step. The firmware itself is built with idf.py (CMake), not make.

MAKEFLAGS += --no-print-directory

.PHONY: flash enter-flash

# Button-less flash: enter download mode, then flash the re-enumerated port.
# The ESP-IDF environment is sourced so idf.py resolves inside scripts/flash.py.
flash:
	@bash -c 'source "$$HOME/.espressif/v6.0.1/esp-idf/export.sh" && python3 scripts/flash.py'

# PORT is optional: pass it (PORT=/dev/cu.usbmodem...), or the script
# auto-detects the first /dev/cu.usbmodem* device.
enter-flash:
	@python3 scripts/enter_download.py $(PORT)
