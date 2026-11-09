#!/usr/bin/env python3
"""Send one bring-up command to the board and print the reply.

Usage: tools/bench.py <cmd> [port]
Run with PlatformIO's python (has pyserial): ~/.platformio/penv/bin/python tools/bench.py p
"""
import glob
import importlib.util
import os
import sys
import time

import serial


def release_helper_port(seconds=600):
    """Ask the Hammerspoon helper to free the port (no-op without the hs CLI)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "release_helper.py")
    spec = importlib.util.spec_from_file_location("release_helper", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if mod.release(seconds=seconds):
        time.sleep(0.5)


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port - board not connected?")
    return ports[0]


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "?"
    port = sys.argv[2] if len(sys.argv) > 2 else find_port()
    release_helper_port()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr, s.rts = True, False  # DTR high = host present; RTS low avoids reset-to-boot patterns
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(cmd.encode())
    buf, deadline = "", time.time() + 60
    while time.time() < deadline:
        buf += s.read(512).decode(errors="replace")
        if "OK>" in buf:
            break
    s.close()
    print(buf.replace("OK>", "").strip() or "(no reply)")


if __name__ == "__main__":
    main()
