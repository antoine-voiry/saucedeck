#!/usr/bin/env python3
"""Pretend to be the Mac helper on the deck's USB serial port (bench tool).

Usage: ~/.platformio/penv/bin/python -I tools/helper_sim.py <volume%> <mic%> [seconds] [--junk] [--clock] [--card=SLOT:ICON:TEXT ...] [--dry]
Sends framed HELLO every second plus VOLUME / MIC (and TIME / CARD when asked), prints any
deck->host messages. --dry prints the frames it would send and exits (no port needed).
Opens at 115200 (never 1200: that resets the deck into its bootloader).
"""
import datetime, glob, random, struct, sys, time
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

ICONS = {"none": 0, "music": 1, "calendar": 2, "cpu": 3, "weather": 4, "mail": 5, "chat": 6, "battery": 7}

def time_frame():
    off = int(datetime.datetime.now().astimezone().utcoffset().total_seconds() // 60)
    return frame(0x04, list(struct.pack("<Ih", int(time.time()), off)))

def card_frame(spec):
    slot, icon, text = spec.split(":", 2)
    icon = ICONS[icon] if icon in ICONS else int(icon)
    t = text.encode("ascii", "replace")[:48]  # deck accepts printable ASCII only
    return frame(0x05, [int(slot), icon, 0, 0, len(t)] + list(t))

NAMES = {0x81: "ACTION", 0x82: "MIC_DELTA", 0x83: "MIC_MUTE"}

def main():
    vol, mic = int(sys.argv[1]), int(sys.argv[2])
    secs = float(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else 10
    junk = "--junk" in sys.argv
    clock = "--clock" in sys.argv
    cards = [card_frame(a[len("--card="):]) for a in sys.argv if a.startswith("--card=")]
    if "--dry" in sys.argv:
        for f in ([time_frame()] if clock else []) + cards:
            print(f.hex())
        return
    port = sorted(glob.glob("/dev/cu.usbmodem*"))[0]
    s = serial.Serial(port, 115200, timeout=0.05)
    buf = b""
    end, nxt = time.time() + secs, 0.0
    while time.time() < end:
        if time.time() >= nxt:
            if junk:
                s.write(bytes(random.getrandbits(8) for _ in range(200)))
            s.write(frame(0x01, [1]) + frame(0x02, [vol, 0]) + frame(0x03, [mic, 0]))
            if clock:
                s.write(time_frame())
            for c in cards:
                s.write(c)
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
