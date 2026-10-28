#!/usr/bin/env python3
"""Reset the board over USB-Serial-JTAG and print its console for a few seconds.

Usage: serial_log.py PORT [SECONDS] [--no-reset]
"""
import sys
import time

import serial

port = sys.argv[1]
duration = float(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 8.0
reset = "--no-reset" not in sys.argv

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.1
ser.dtr = False
ser.rts = False
ser.open()

if reset:
    # USB-Serial-JTAG: RTS asserted with DTR released resets the chip
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False

end = time.time() + duration
while time.time() < end:
    try:
        data = ser.read(4096)
    except serial.SerialException:
        # The port disappears briefly while the chip re-enumerates after reset
        ser.close()
        time.sleep(0.3)
        try:
            ser.open()
        except serial.SerialException:
            pass
        continue
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
