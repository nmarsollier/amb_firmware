# amb_firmware — flash helper
#
#   make flash         flash the firmware

PORT   ?= /dev/cu.usbserial-1410      # USB-Serial/JTAG (flash/monitor)

.PHONY: flash

# Flash the firmware.
flash:
	cd .. && source ~/.espressif/v6.0.1/esp-idf/export.sh && idf.py -p $(PORT) flash
