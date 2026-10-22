#!/usr/bin/env python3
"""Pretend to be the Mac helper on the deck's USB serial port (bench tool).

Usage: ~/.platformio/penv/bin/python -I tools/helper_sim.py <volume%> <mic%> [seconds] [--junk]
Sends framed HELLO every second plus VOLUME / MIC, prints any deck->host messages.
Opens at 115200 (never 1200: that resets the deck into its bootloader).
"""
import glob, random, sys, time
import serial

def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c

def frame(mtype, payload):
    msg = bytes([1, mtype, len(payload)]) + bytes(payload)
    msg += bytes(63 - len(msg))
    c = crc16(msg)
    return bytes([0xA5, 0x5A]) + msg + bytes([c & 0xFF, c >> 8])

NAMES = {0x81: "ACTION", 0x82: "MIC_DELTA", 0x83: "MIC_MUTE"}

def main():
    vol, mic = int(sys.argv[1]), int(sys.argv[2])
    secs = float(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else 10
    junk = "--junk" in sys.argv
    port = sorted(glob.glob("/dev/cu.usbmodem*"))[0]
    s = serial.Serial(port, 115200, timeout=0.05)
    buf = b""
    end, nxt = time.time() + secs, 0.0
    while time.time() < end:
        if time.time() >= nxt:
            if junk:
                s.write(bytes(random.getrandbits(8) for _ in range(200)))
            s.write(frame(0x01, [1]) + frame(0x02, [vol, 0]) + frame(0x03, [mic, 0]))
            nxt = time.time() + 1.0
        buf += s.read(256)
        while len(buf) >= 67:
            i = buf.find(b"\xA5\x5A")
            if i < 0:
                buf = buf[-1:]
                break
            buf = buf[i:]
            if len(buf) < 67:
                break
            msg, c = buf[2:65], buf[65] | buf[66] << 8
            if crc16(msg) != c:
                buf = buf[1:]
                continue
            buf = buf[67:]
            t = msg[1]
            if t == 0x81:
                print(f"deck: ACTION key={msg[3] + 1} type={msg[4]} value={msg[6:6 + msg[5]].decode()!r}")
            else:
                print(f"deck: {NAMES.get(t, hex(t))} {list(msg[3:3 + msg[2]])}")
    print("done")

if __name__ == "__main__":
    main()
