# NEMU Port & Licensing Audit (Milestone 2)

Goal: identify which open emulator components may be legally ported/grafted into
NEMU, with license compatibility verified, and a documented integration plan.

## 1. NEMU's own license

- NEMU is **GPL-3.0-or-later** (see `LICENSE`). This is the decisive fact for
  port-in compatibility.

## 2. License-compatibility matrix (what may be ported INTO NEMU)

| Inbound source license | Compatible with NEMU (GPL-3.0+)? | Notes |
| :--- | :--- | :--- |
| **GPL-3.0 / GPL-3.0-or-later** | YES | License-compatible. Must retain GPLv3 notices + attribution. |
| **GPL-2.0-only** | **NO** | GPLv2-only and GPLv3 are mutually incompatible; cannot mix. |
| **GPL-2.0-or-later** | YES | 'or later' permits GPLv3. |
| **MIT / MIT-0** | YES | Permissive; compatible. Add notice. |
| **Apache-2.0** | YES | Compatible; Apache-2.0 code into GPLv3 is fine (one-way). Add NOTICE. |
| **BSD-2/3-Clause / ISC / Zlib** | YES | Permissive; compatible. Add notice. |
| **LGPL** | Conditional | Only as a separately-linked library, not merged source. |
| **CC-BY-SA / CC-BY-NC** | NO / avoid | Copyleft or non-commercial; avoid. |

## 3. Candidate sources (research; VERIFY SPDX at pull time — licenses change)

- **Ryujinx** — historically **MIT**. Its service/kernel/format parsing and
  CPU/GPU code is a strong permissive candidate for many subsystems.
- **Strato / hikari (Android)** — **GPL-3.0**. Compatible; high-quality kernel,
  FS (arrow/plutonium), audio (tegra-audioutils), graphics.
- **yuzu / suyu remnants** — historically **GPL-2.0+** (SPDX "GPL-2.0+
  OR GPL-3.0" in places; project is defunct/contested). Treat as **GPL-2.0-or-later**
  only where the file header explicitly says so; verify per-file.
- **Eden / NeXium** — new experimental projects; verify their licenses at pull
  time before considering.

## 4. NON-NEGOTIABLE constraint

GPL/license compatibility does NOT grant rights to Nintendo's copyrighted
**prod.keys / title keys / firmware** or bundled game assets. NEMU stays
clean-room on all Nintendo material. Real commercial-game decryption only ever
proceeds with keys the user legally dumps from their own console at runtime; we
do not bundle or redistribute any.

## 5. Highest-value legal integration candidates (by NEMU need)

1. **Horizon kernel/IPC service reply fidelity** — port permissive (MIT) or GPLv3
   service impls (sm, set, time, fs, ns, applet) to replace stubs that block real
   boot. Biggest payoff for "boot".
2. **File-format / container parsing** (NRO/NSO relocation, NCA/PFS0/RomFS exact
   semantics) — permissive libraries are common here.
3. **ARM64 JIT backend** — Ryujinx's ARMeilleure (MIT) or Strato's hikari (GPLv3)
   are far more complete than NEMU's current dynarec; both license-compatible.
4. **Texture/codec** (ASTC, BC7, NVNC) — permissive decoders exist (e.g. various
   MIT ASTC decoders); drop-in.

## 6. Integration plan

1. For each candidate, clone/reference, confirm SPDX per-file, record in THIS doc.
2. Add a `third_party/` dir with per-project LICENSE/NOTICE files.
3. Port the smallest high-value unit first (e.g., an exact RomFS/NSO parser or a
   set:sys/time:u reply), keeping interfaces to NEMU's `title_loader`/IPC.
4. Add unit tests mirroring NEMU's existing per-subsystem tests.
5. Buddy-build both Linux and Windows/Xbox cross-`build-win`.

## 7. Status

- [x] License-compatibility framework (this doc).
- [ ] Per-file SPDX verification for chosen candidate repo at pull time.
- [ ] First integration (pick one: exact NSO relocation, or set/time IPC replies).