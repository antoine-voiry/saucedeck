# PlatformIO extra script for coverage envs. build_flags already passes --coverage to every
# compile (project and libraries); PlatformIO does not pass it to the linker, so add it here.
# Without it the link fails with "Undefined symbols: ___llvm_gcov_writeout" (seen 2026-10-24).
Import("env")
env.Append(LINKFLAGS=["--coverage"])
