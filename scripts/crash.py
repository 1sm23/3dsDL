#!/usr/bin/env python3
"""Names the code in a 3dsDL crash report.

    scripts/crash.py crash.txt
    scripts/crash.py crash_dump_00000003.dmp --version 0.8.2
    scripts/crash.py crash.txt --elf dist/3dsDL.elf

Reads sdmc:/3ds/3dsDL/crash.txt (written by 3dsDL 0.8.2 on) or a Luma3DS
dump from sdmc:/luma/dumps/arm11/, finds the 3dsDL.elf of that version (the
release's, downloaded once into ~/.cache/3dsDL/, or dist/3dsDL.elf when
VERSION matches) and prints where the crash was and the return addresses on
the stack as functions and lines. addr2line comes from devkitARM, from PATH,
or from the build image in Docker; without any, functions are named from the
ELF's symbols alone.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASES = "https://github.com/ankushKun/3dsDL/releases/download"
FORK_RELEASES = "https://github.com/1sm23/3dsDL/releases/download"
LUMA_TYPES = {1: "undefined instruction", 2: "prefetch abort", 3: "data abort"}
LUMA_REGISTERS = [f"r{n}" for n in range(13)] + [
    "sp", "lr", "pc", "cpsr", "dfsr", "ifsr", "far", "fpexc", "fpinst", "fpinst2"]


def fail(message):
    sys.exit(f"crash.py: {message}")


# --- Reports -------------------------------------------------------------------


def read_crash_log(data):
    """crash.txt: "key: value" lines; screen, stack and log repeat."""
    report = {"kind": "crash.txt", "screens": [], "stack": [], "registers": {},
              "log": []}
    for line in data.decode("utf-8", "replace").splitlines():
        key, _, value = line.partition(": ")
        if key == "screen":
            report["screens"].append(value)
        elif key == "log":
            report["log"].append(value)
        elif key == "stack":
            report["stack"] += [int(word, 16) for word in value.split()]
        elif key in LUMA_REGISTERS or key == "fsr":
            report["registers"][key] = int(value, 16)
        elif key in ("version", "build", "time", "what", "thread", "free"):
            report[key] = value
    if "version" not in report:
        fail("this is not a 3dsDL crash.txt")
    return report


def read_luma_dump(data):
    """A Luma3DS ARM11 exception dump (exceptions.h, ExceptionDumpHeader)."""
    (_, _, minor, major, processor, _, kind, _, register_size, code_size,
     stack_size, extra_size) = struct.unpack_from("<2I4H6I", data)
    if processor != 11:
        fail("this dump is from the ARM9, not 3dsDL")
    offset = 40
    words = struct.unpack_from(f"<{register_size // 4}I", data, offset)
    registers = dict(zip(LUMA_REGISTERS, words))
    offset += register_size
    code = data[offset:offset + code_size]
    offset += code_size
    stack = data[offset:offset + stack_size]
    offset += stack_size
    report = {"kind": f"Luma3DS dump v{major}.{minor}", "registers": registers,
              "what": LUMA_TYPES.get(kind, f"exception {kind}"),
              "code": code, "screens": [],
              "stack_words": list(struct.unpack(f"<{len(stack) // 4}I",
                                                stack[:len(stack) // 4 * 4]))}
    if extra_size >= 16:
        name = data[offset:offset + 8].split(b"\0")[0].decode("ascii", "replace")
        title, = struct.unpack_from("<Q", data, offset + 8)
        report["process"] = f"{name}, {title:016X}"
    return report


def read_report(path):
    with open(path, "rb") as file:
        data = file.read()
    if data[:8] == struct.pack("<2I", 0xDEADC0DE, 0xDEADCAFE):
        return read_luma_dump(data)
    return read_crash_log(data)


# --- The ELF -------------------------------------------------------------------


class Elf:
    def __init__(self, path):
        with open(path, "rb") as file:
            self.data = file.read()
        if self.data[:4] != b"\x7fELF" or self.data[4] != 1:
            fail(f"{path} is not a 32-bit ELF")
        (phoff, shoff, _, _, phentsize, phnum, shentsize, shnum,
         _) = struct.unpack_from("<3I6H", self.data, 28)
        self.segments = []  # (address, size, file offset) of code
        for index in range(phnum):
            (kind, offset, address, _, filesz, _, flags,
             _) = struct.unpack_from("<8I", self.data, phoff + index * phentsize)
            if kind == 1 and flags & 1:  # PT_LOAD, executable
                self.segments.append((address, filesz, offset))
        self.symbols = []  # (address, size, name) of functions
        sections = [struct.unpack_from("<10I", self.data, shoff + index * shentsize)
                    for index in range(shnum)]
        for section in sections:
            if section[1] != 2:  # SHT_SYMTAB
                continue
            strings = sections[section[6]]
            for at in range(section[4], section[4] + section[5], 16):
                name, value, size, info, _, _ = struct.unpack_from(
                    "<3I2BH", self.data, at)
                if info & 0xF == 2 and value:  # STT_FUNC
                    end = self.data.index(b"\0", strings[4] + name)
                    self.symbols.append((value & ~1, size,
                                         self.data[strings[4] + name:end].decode()))
        self.symbols.sort()

    def code(self, address, length):
        for start, size, offset in self.segments:
            if start <= address and address + length <= start + size:
                return self.data[offset + address - start:
                                 offset + address - start + length]
        return None

    def in_code(self, address):
        return any(start <= address < start + size
                   for start, size, _ in self.segments)

    def after_call(self, address):
        """Whether address follows a call, as a return address does."""
        if address & 1:  # Thumb
            before = self.code((address & ~1) - 4, 4)
            if before is None:
                return False
            first, second = struct.unpack("<2H", before)
            return ((first & 0xF800 == 0xF000 and second & 0xC000 == 0xC000) or
                    second & 0xFF87 == 0x4780)
        before = self.code(address - 4, 4)
        if before is None:
            return False
        word, = struct.unpack("<I", before)
        return ((word & 0x0F000000 == 0x0B000000 and word >> 28 != 0xF) or
                word & 0xFE000000 == 0xFA000000 or
                word & 0x0FFFFFF0 == 0x012FFF30)

    def function(self, address):
        best = None
        for start, size, name in self.symbols:
            if start > address:
                break
            if address < start + max(size, 1):
                best = name
        return best


def find_elf(version):
    """The release's ELF; a local build's only for a version not released."""
    cache = os.path.join(os.path.expanduser(os.environ.get("XDG_CACHE_HOME", "~/.cache")),
                         "3dsDL", version)
    path = os.path.join(cache, "3dsDL.elf")
    if os.path.exists(path):
        return path
    releases = FORK_RELEASES if version.endswith("n") else RELEASES
    url = f"{releases}/v{version}/3dsDL.elf"
    print(f"Downloading {url}", file=sys.stderr)
    os.makedirs(cache, exist_ok=True)
    try:
        with urllib.request.urlopen(url) as response, open(path + ".part", "wb") as out:
            shutil.copyfileobj(response, out)
    except OSError as error:
        with open(os.path.join(ROOT, "VERSION")) as file:
            local = file.read().strip()
        built = os.path.join(ROOT, "dist", "3dsDL.elf")
        if version == local and os.path.exists(built):
            print(f"Not released; using {os.path.relpath(built)}", file=sys.stderr)
            return built
        fail(f"no 3dsDL.elf for {version} ({error}). Older releases have "
             "none: run make at that tag in the build image named in "
             "scripts/build.sh, then pass --elf.")
    os.replace(path + ".part", path)
    return path


# --- addr2line -----------------------------------------------------------------


def addr2line_command(elf):
    for candidate in (os.path.join(os.environ.get("DEVKITARM", ""), "bin",
                                   "arm-none-eabi-addr2line"),
                      shutil.which("arm-none-eabi-addr2line") or ""):
        if candidate and os.access(candidate, os.X_OK):
            return [candidate, "-e", elf]
    if shutil.which("docker"):
        with open(os.path.join(ROOT, "scripts", "build.sh")) as file:
            image = re.search(r'THREEDSDL_BUILD_IMAGE:-([^}"]+)', file.read()).group(1)
        folder = os.path.dirname(os.path.abspath(elf))
        return ["docker", "run", "--rm", "-v", f"{folder}:/symbols:ro", image,
                "/opt/devkitpro/devkitARM/bin/arm-none-eabi-addr2line",
                "-e", f"/symbols/{os.path.basename(elf)}"]
    return None


def locate(elf, path, addresses):
    """{address: [(function, file:line), ...]}, inlined callers last."""
    wanted = sorted(set(address & ~1 for address in addresses))
    found = {address: [(elf.function(address) or "??", "")] for address in wanted}
    command = addr2line_command(path)
    if command is None or not wanted:
        return found
    try:
        output = subprocess.run(
            command + ["-f", "-C", "-i", "-a"] + [f"{a:08x}" for a in wanted],
            capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"addr2line failed ({error}); naming functions only.", file=sys.stderr)
        return found
    current = None
    lines = output.splitlines()
    index = 0
    while index < len(lines):
        if lines[index].startswith("0x"):
            current = int(lines[index], 16)
            found[current] = []
            index += 1
            continue
        where = lines[index + 1] if index + 1 < len(lines) else "??:0"
        where = re.sub(r"^.*?/source/", "source/", where)
        found[current].append((lines[index], where))
        index += 2
    return found


# --- Output --------------------------------------------------------------------


def show(label, address, places):
    for depth, (function, where) in enumerate(places[address & ~1]):
        prefix = f"{label:<8}{address:08X}" if depth == 0 else " " * 16
        inlined = "" if depth == 0 else "inlined in "
        print(f"{prefix}  {inlined}{function}  {where}".rstrip())


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("report", help="crash.txt or a Luma3DS .dmp")
    parser.add_argument("--version", help="the 3dsDL version that crashed "
                        "(a .dmp does not say)")
    parser.add_argument("--elf", help="the 3dsDL.elf to use")
    args = parser.parse_args()

    report = read_report(args.report)
    version = args.version or report.get("version")
    if not args.elf and not version:
        fail("a Luma3DS dump does not say its version: pass --version or --elf")
    path = args.elf or find_elf(version)
    elf = Elf(path)
    registers = report["registers"]

    if "stack_words" in report:
        report["stack"] = [word for word in report["stack_words"] if elf.in_code(word)]
    returns = [word for word in report["stack"] if elf.after_call(word)]

    pcs = []
    if "pc" in registers:
        pc = registers["pc"]
        pcs.append(("pc", pc))
        if report["kind"] == "crash.txt":
            # The kernel may pass pc unadjusted: then the fault is before it.
            back = {"data abort": 8}.get(report.get("what"), 4)
            pcs.append((f"pc-{back}", pc - back))
    lr = registers.get("lr")
    places = locate(elf, path, [a for _, a in pcs] + ([lr] if lr else []) + returns)

    heading = [f"3dsDL {version or '?'}"]
    if report.get("build"):
        heading.append(f"({report['build']})")
    print(" ".join(heading) + f", {report.get('what', 'crash')}"
          + (f" on the {report['thread']} thread" if report.get("thread") else "")
          + (f", {report['time']}" if report.get("time") else ""))
    shown = os.path.relpath(path)
    if shown.startswith(".."):
        shown = path.replace(os.path.expanduser("~"), "~", 1)
    print(f"From {report['kind']}, symbols {shown}")
    if report.get("process"):
        print(f"Process {report['process']}")
    if report["screens"]:
        print("Screens " + "  <  ".join(report["screens"]))
    if report.get("free"):
        print(f"Free    {int(report['free']) / 1048576:.2f} MiB of application memory")
    if "far" in registers and report.get("what") in ("data abort", "prefetch abort"):
        fsr = registers.get("fsr", registers.get(
            "ifsr" if report["what"] == "prefetch abort" else "dfsr", 0))
        print(f"Fault   {registers['far']:08X} (fsr {fsr:X})")
    if "code" in report and "pc" in registers:
        thumb = registers.get("cpsr", 0) & 0x20
        start = registers["pc"] + (2 if thumb else 4) - len(report["code"])
        matches = elf.code(start, len(report["code"])) == report["code"]
        print("Code    " + ("matches this ELF" if matches else
                            "DOES NOT match this ELF: wrong version?"))
    print()
    for label, address in pcs:
        show(label, address, places)
    if lr:
        show("lr", lr, places)
    if returns:
        print("\nReturn addresses on the stack, innermost first:")
        for number, address in enumerate(returns):
            show(f"#{number}", address, places)
    elif not registers:
        print("No return addresses on the stack.")
    if report.get("log"):
        print("\nLast log lines:")
        for line in report["log"]:
            print("  " + line)


if __name__ == "__main__":
    main()
