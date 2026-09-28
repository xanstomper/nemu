# Running Commercial Games — Complete Guide

This is the definitive guide to running retail Switch software on Nemulator:
the file formats it loads, the keys it needs, the exact directory layout, and
what to expect at each stage. It pairs with
[`BOOT_READINESS_AUDIT.md`](BOOT_READINESS_AUDIT.md) (the verified technical
chain) and [`XBOX_QUICKSTART.md`](XBOX_QUICKSTART.md) (bring-up).

---

## 1. What Nemulator Loads

| Format | Magic | What it is | Loaded by |
| :--- | :--- | :--- | :--- |
| **NCA** | `NCA3`/`NCA2`/`NCA0` | encrypted install media container | `nca.cpp` |
| **NSO** | `NSO0` | executable (decompressed + relocated) | `nso.cpp` |
| **NRO** | `NRO0` | homebrew executable | `nro.cpp` |
| **PFS0 / ExeFS** | `PFS0` | `.nsp` / ExeFS container | `pfs0.cpp` |
| **RomFS** | IVFC | game read-only FS inside an NCA | `romfs.cpp` |

`nso.cpp` performs **LZ4 decompression** and applies **`R_AARCH64_RELATIVE`
relocations**; `nca.cpp` performs **AES-XTS** header decryption and **CTR**
section decryption.

---

## 2. Keys (`keys/`)

Nemulator ships **zero** proprietary keys. Commercial (encrypted) media needs
the keyset you extract from a console you own. Place them in the repo root:

```
nemu/
├── keys/
│   ├── prod.keys      # the master production key file (from your console)
│   └── title.keys     # per-title keys (optional; many titles are in prod.keys)
```

The `KeyStore` (`src/core/crypto/key_store.cpp`) reads these automatically.
`LoadTitle` also scans the title's directory for a `*.tik` ticket and applies
it for key generation.

> **Legal note:** only use keys you legitimately extracted from hardware you
> own. Do not redistribute `prod.keys`/`title.keys`.

---

## 3. Game File Layout (on Xbox / USB)

Recommended directory on the console (USB `E:\` or internal):

```
E:\nemu\
├── sdmc\
│   └── <title>\          # your games (NCA/NSP/NSO) live here
│       ├── title.nca      # or title.nsp / main.nso
│       └── title.tik      # ticket beside the title (optional)
└── saves\                # SaveManager destination
```

Because the AppX declares `runFullTrust` + `broadFileSystemAccess`, Nemulator
can read arbitrary paths on console/USB storage.

---

## 4. Launching

### Headless boot probe (all platforms)

```bash
Nemu --run <path-to-title> --max-frames=60
# [NEMU-BOOT] ... -> BOOTED (advanced frames)     = good
# [NEMU-BOOT] ... -> NO-FRAMES (stalled)          = investigation needed
```

### On the Xbox (via the QA harness)

```bash
scripts/xbox_bringup.sh <XBOX_IP> mytitle.nso 60
```

### Interactive (packaged app)

Launch Nemulator from Dev Home, browse the game library, or use the Settings
app to pick the title.

---

## 5. What "It Booting" Requires, Top to Bottom

1. **Valid keys** for the specific title → decrypt the NCA header + CTR sections.
2. **LZ4 + relocations** applied so the NSO's branches/pointers are correct.
3. **The JIT** executes the entry point, trapping into the kernel.
4. **Boot-time syscalls** — the full 49-SVC surface (threading, memory, handles).
5. **IPC services** bind via `sm:` → `fsp-srv`, `hid`, `vi`, `applet`, etc. —
   Nemulator registers all 54.
6. **Shader translation** — the title's Maxwell SASS is translated to HLSL/D3D12
   (all 162 families recognized).
7. **Rendering** — MRT, depth-stencil, textures (ASTC→BC1), present pipeline.

---

## 6. Expected Behavior & Troubleshooting

| Symptom | Likely cause | Action |
| :--- | :--- | :--- |
| `FAILED: could not load title` | keys missing → NCA header won't decrypt | add `keys/prod.keys` for the title |
| Boots then `NO-FRAMES (stalled)` | a service call/syscall the title needs not yet handled | capture trace; report the SVC/IPC id |
| `[untranslated] SASS:<op>` in shader log | one of the 40 rarest families | cosmetic — rare in retail shaders |
| Black screen on hardware | App Type not `Game` | set **App Type = Game** in Dev Portal |
| RAM near the cap | texture budget too big for the title | `Nemu --texture-budget=1024` |

---

## 7. Honest Scope

- **Homebrew and path-through titles** are the primary day-one targets.
- **Retail NCA/XCI** boot is implemented end-to-end, but each title's exact
  syscall/IPC/shader/relocation mix is unique — on-device bring-up reveals the
  per-title deltas, and the live trace (via `scripts/xbox_bringup.sh`) is the
  debugging tool.
- The **40 rarest SASS families** are auditable diagnostics, not executed.

---

*Chain details: [`BOOT_READINESS_AUDIT.md`](BOOT_READINESS_AUDIT.md) ·
Optimizations: [`OPTIMIZATION_PLAYBOOK.md`](OPTIMIZATION_PLAYBOOK.md) ·
Bring-up: [`XBOX_QUICKSTART.md`](XBOX_QUICKSTART.md)*