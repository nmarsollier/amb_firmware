# amb_firmware — build and flash one connected ESP32-S3.
# flash: build, request bootloader, verify ROM, then flash.
# flash-download: build and flash a board already in ROM download mode.
# enter-flash: enter and verify download mode without building or flashing.
# PORT is optional when exactly one /dev/cu.usbmodem* exists.

MAKEFLAGS += --no-print-directory

.PHONY: flash flash-download enter-flash

flash:
	@bash -c 'source "$$HOME/.espressif/v6.0.1/esp-idf/export.sh" && python3 scripts/flash.py $(PORT)'

flash-download:
	@bash -c 'source "$$HOME/.espressif/v6.0.1/esp-idf/export.sh" && python3 scripts/flash.py --download $(PORT)'

enter-flash:
	@bash -c 'source "$$HOME/.espressif/v6.0.1/esp-idf/export.sh" && python3 scripts/enter_download.py $(PORT)'
