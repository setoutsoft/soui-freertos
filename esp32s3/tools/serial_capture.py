"""Capture the COM4 boot log of the SOUI firmware for `seconds` after a reset.

Kept out of the build tree: this is a diagnostic helper, not firmware code.

Rationale for the DTR/RTS dance: the ESP32-S3 on this board uses the native
USB-Serial/JTAG PHY, where esptool resets the chip by asserting both lines --
same sequence as `esptool --before default_reset`.
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0

# 115200 8N1 is what CONFIG_ESP_CONSOLE_UART_BAUDRATE sets.
ser = serial.Serial(PORT, 115200, timeout=0.2)

# Reset into run mode: EN low, IO0 high, EN high.
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.12)
ser.setRTS(False)
time.sleep(0.05)
ser.reset_input_buffer()

deadline = time.time() + SECONDS
chunks = []
while time.time() < deadline:
    data = ser.read(4096)
    if data:
        chunks.append(data)

ser.close()

sys.stdout.buffer.write(b"".join(chunks))
