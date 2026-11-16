"""Continuously print what the board is logging (Ctrl+C to stop).

Why this exists next to serial_capture.py:
  * serial_capture.py always pulses DTR/RTS to reset the chip and then exits
    after N seconds -- good for "capture a boot sequence", useless for
    "watch the log while the UI runs".
  * `idf.py monitor` is the normal answer but refuses to run under Git Bash
    ("MSys/Mingw is no longer supported") and export.ps1 is broken in IDF
    5.4.1 (it passes "powershell.EXE" and activate.py's whitelist only has
    "powershell.exe"), so on this machine the shortest path to a live console
    is this script + the IDF python that already has pyserial installed:
        tools/.espressif/python_env/idf5.4_py3.13_env/Scripts/python.exe

Console configuration on this board (sdkconfig):
    CONFIG_ESP_CONSOLE_UART_DEFAULT=y               UART0 @ 115200
    CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y  also to the native USB
So COM4 (the USB-Serial/JTAG port) sees the same ESP_LOG stream as the UART
pins.  The baud rate on that port is nominal -- it is CDC, not a real UART --
but 115200 is what CONFIG_ESP_CONSOLE_UART_BAUDRATE says, so use it.

Usage:
    <idf python> tools/serial_monitor.py                 # COM4, watch only
    <idf python> tools/serial_monitor.py COM5            # another port
    <idf python> tools/serial_monitor.py --reset         # reset first
    <idf python> tools/serial_monitor.py --seconds 20    # stop on its own
    <idf python> tools/serial_monitor.py --log run.log   # tee to a file

Note: the port is exclusive.  Close this before flashing (esptool opens COM4)
and before running tools/serial_capture.py, and vice versa.
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("port", nargs="?", default="COM4", help="serial port (default COM4)")
    ap.add_argument("--baud", type=int, default=115200,
                    help="nominal only on USB-Serial/JTAG (default 115200)")
    ap.add_argument("--reset", action="store_true",
                    help="pulse DTR/RTS to reset the chip first (default: leave it running)")
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="stop after N seconds (default 0 = until Ctrl+C)")
    ap.add_argument("--log", default=None, help="also append everything to this file")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as e:
        print("cannot open %s: %s" % (args.port, e), file=sys.stderr)
        print("  - is another program holding it? (monitor / esptool / "
              "serial_capture.py / a GUI terminal)", file=sys.stderr)
        print("  - does the port still exist? check Device Manager", file=sys.stderr)
        return 1

    # Same reset sequence as serial_capture.py, and only when asked: opening the
    # port must not disturb a board that is already running.
    if args.reset:
        ser.setDTR(False)
        ser.setRTS(True)
        time.sleep(0.12)
        ser.setRTS(False)
        time.sleep(0.05)

    ser.reset_input_buffer()

    out = sys.stdout.buffer
    logf = open(args.log, "ab") if args.log else None
    deadline = (time.time() + args.seconds) if args.seconds > 0 else None

    print("--- %s @ %d, %s, Ctrl+C to stop%s" % (
        args.port, args.baud,
        "reset" if args.reset else "no reset",
        ("" if not args.log else ", logging to " + args.log)), file=sys.stderr)

    total = 0
    t0 = time.time()
    try:
        while True:
            data = ser.read(4096)
            if data:
                out.write(data)
                out.flush()
                if logf:
                    logf.write(data)
                    logf.flush()
                total += len(data)
            if deadline and time.time() >= deadline:
                break
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if logf:
            logf.close()
        el = time.time() - t0
        print("--- stopped: %d bytes in %.1fs" % (total, el), file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
