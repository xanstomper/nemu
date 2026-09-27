#!/usr/bin/env python3
"""Regenerate sass_opcode_table.inc + sass_cute_names.inc from yuzu's maxwell.inc.

Source: https://github.com/shadowdelsus/yuzu (GPL-3.0-or-later mirror),
file src/shader_recompiler/frontend/maxwell/maxwell.inc.

Usage: python3 tools/gen_sass_table.py [/path/to/maxwell.inc]
Output: src/core/gpu/shader/{sass_opcode_table,sass_cute_names}.inc
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHADER_DIR = os.path.join(HERE, "..", "src", "core", "gpu", "shader")


def main():
    src_path = None
    if len(sys.argv) > 1:
        src_path = sys.argv[1]
    else:
        url = ("https://raw.githubusercontent.com/shadowdelsus/yuzu/master/"
               "src/shader_recompiler/frontend/maxwell/maxwell.inc")
        tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".inc")
        subprocess.run(["curl", "-sL", url, "-o", tmp.name], check=True)
        src_path = tmp.name

    insts = []
    for line in open(src_path, encoding="utf-8"):
        m = re.match(r'INST\((\w+),\s*"([^"]+)",\s*"([^"]+)"\)', line.strip())
        if m:
            name, disp, pattern = m.groups()
            insts.append((name, disp, pattern.replace(" ", "")))

    def enc16(chars):
        mask = value = 0
        for c in chars:
            mask = (mask << 1) | (c != "-")
            value = (value << 1) | (c == "1")
        return mask << (64 - len(chars)), value << (64 - len(chars))

    rows = []
    for name, disp, bits in insts:
        m64, v64 = enc16(bits)
        base = re.sub(r"_(reg|cbuf|imm|rc|cr|cas)$", "", name)
        rows.append({"inst": name, "base": base, "disp": disp,
                     "mask": m64, "value": v64,
                     "specificity": bits.count("0") + bits.count("1")})
    rows.sort(key=lambda r: -r["specificity"])

    fams = sorted({r["base"] for r in rows})

    out = []
    out.append("// SASS major-opcode match table — %d encodings / %d families."
               % (len(rows), len(fams)))
    out.append("// Ported from yuzu shader_recompiler/frontend/maxwell "
               "(maxwell.inc, GPL-3.0-or-later).")
    out.append("// Encoding: MSB-first from bit 63; match = (inst & mask) == value.")
    out.append("// Ordered most-specific first (yuzu SortedEncodings popcount order).")
    out.append("")
    out.append("enum class SassOpcode : u32 {")
    for i, f in enumerate(fams):
        out.append("    %s = %d," % (f, i))
    out.append("};")
    out.append("")
    out.append("struct SassEncoding {")
    out.append("    u64 mask;")
    out.append("    u64 value;")
    out.append("    SassOpcode opcode;")
    out.append("};")
    out.append("")
    out.append("inline constexpr std::array<SassEncoding, %d> kSassEncodings{"
               % len(rows))
    for r in rows:
        out.append("    {0x%016XULL, 0x%016XULL, SassOpcode::%s}, // %s"
                   % (r["mask"], r["value"], r["base"], r["disp"]))
    out.append("};")
    open(os.path.join(SHADER_DIR, "sass_opcode_table.inc"), "w").write(
        "\n".join(out))

    nout = []
    nout.append("// Parallel cute-name array for kSassEncodings (generated together —")
    nout.append("// do not edit; regenerate with tools/gen_sass_table.py).")
    nout.append("inline constexpr std::array<const char*, %d> kSassCuteNames{"
                % len(rows))
    for r in rows:
        nout.append('    "%s",' % r["disp"])
    nout.append("};")
    open(os.path.join(SHADER_DIR, "sass_cute_names.inc"), "w").write(
        "\n".join(nout))

    print("regenerated: %d encodings, %d families" % (len(rows), len(fams)))


if __name__ == "__main__":
    main()
