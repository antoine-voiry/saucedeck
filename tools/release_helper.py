"""PlatformIO pre-upload hook: ask the Hammerspoon helper to free the deck's serial port.

The 1200 bps touch that resets the deck into its bootloader cannot open the port while the
helper holds it. Missing `hs` CLI or helper -> upload proceeds as before.
"""
import shutil, subprocess, time


def release(which=shutil.which, run=subprocess.run, seconds=None):
    hs = which("hs")
    if not hs:
        return False
    call = "saucedeck.release()" if seconds is None else "saucedeck.release(%d)" % seconds
    try:
        r = run([hs, "-t", "2", "-c", call], timeout=5, capture_output=True)
    except (subprocess.TimeoutExpired, OSError):
        return False
    return r.returncode == 0


try:
    Import("env")  # noqa: F821 (SCons global, only defined inside PlatformIO)
except NameError:
    env = None

if env is not None:
    def _before_upload(source, target, env):
        if release():
            print("release_helper: Hammerspoon helper released the port")
            time.sleep(0.5)

    env.AddPreAction("upload", _before_upload)
