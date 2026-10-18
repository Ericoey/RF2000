"""Link the bare-metal AVR firmware and validate its actual ELF sections."""
import os
from pathlib import Path
import subprocess
import sys

Import("env")  # Provided by PlatformIO/SCons.

env.AppendUnique(LIBS=["m"])
env.AppendUnique(LINKFLAGS=[
    "-flto",
    "-Wl,--gc-sections",
    "-Wl,-Map=" + env.subst("$BUILD_DIR/${PROGNAME}.map"),
])


def check_size(source, target, env):
    toolchain = Path(env.PioPlatform().get_package_dir("toolchain-atmelavr"))
    size_tool = toolchain / "bin" / ("avr-size.exe" if os.name == "nt" else "avr-size")
    checker = Path(env.subst("$PROJECT_DIR")) / "tools" / "check-avr-size.py"
    subprocess.run([sys.executable, str(checker), str(target[0]), str(size_tool)], check=True)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_size)
