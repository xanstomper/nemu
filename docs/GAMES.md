# Adding Your Games & Keys (Easy Drop-In)

NEMU discovers games the way RetroArch / Eden does: you **drop files into a
folder**, and the on-screen library scans and lists them. You select a card and
hit **Launch / Resume** (F5) — it boots through the whole chain (XCI/NSP unlock →
decrypt → JIT → Horizon HLE → render).

---

## 1 · The drop folder

NEMU auto-creates these and scans them on every boot (look for the **"Scan for
Games"** action too):

| Platform | Drop folder | Virtual path |
|---|---|---|
| **PC (Linux)** | `./games/` (and `./roms/`) next to the emulator | `LOCAL:/games`, `LOCAL:/roms` |
| **Xbox — USB** | `E:\games\`, `F:\games\`, `G:\games\` on an NTFS USB drive | `E:/games`, `F:/games`, `G:/games` |
| **Any** | `sdmc:/games`, `sdmc:/switch` under the SD/Switch folder | `sdmc:/games` |

Example (PC):
```
./games/
├── Hollow.Kingdom.xci        # or .nsp / .nca / .nso / .nro
├── Silksong.nsp
└── prod.keys                 # your own dumped keys (optional, but needed for retail)
```

## 2 · Title keys

- **`prod.keys`** (and/or `title.keys`) are loaded automatically from:
  - `./prod.keys`, `./keys/prod.keys`
  - `save/keys/prod.keys`
  - `sdmc/switch/prod.keys`
  - `~/.switch/prod.keys`
- **Tickets** (`.tik`) are auto-registered from the game's folder and from
  inside `.nsp` packages — just drop them next to your games.
- If keys are missing, NEMU logs a clear warning on startup telling you exactly
  where to place them.

## 3 · The flow

1. Drop your `.xci` / `.nsp` / `.nca` / `.nso` files + `prod.keys` into a drop folder.
2. Launch NEMU — the library auto-scans on boot (or press **Scan Folder**).
3. Your games appear as cards. Select one → **Launch / Resume Selected Title** (F5).
4. If a game won't start, see **[Booting Commercial Games](BOOTING_GAMES.md)** — the
   crash report in `LOCAL:/crash/` + the GDB stub (`--gdb=<port>`) tell you why.

## 4 · About the Xbox Device Portal (honest)

The Device Portal (`https://<xbox-ip>:11443`) is great for **deploying the AppX,
streaming the trace log, and reading crash reports** — but it is **not** a
reliable way to upload multi-GB game files. Xbox Dev Mode's secure, supported
path for your game library is the **USB drive** (`E:\games\`).

To get a game onto the console:
1. Format a USB 3.0 flash/SSD as **NTFS**.
2. Make `E:\games\` (or just drop `.xci`/`.nsp` + `prod.keys` under `E:\`).
3. Plug into the console → NEMU scans `E:/games` on boot → launch from the library.

If you are networking/transferring games for development, use the Device Portal
or a network share to move files onto the USB drive, then rescan.

---

**Open a scan from anywhere:** Settings → **Scan Game Folder** →
`ROOT:/` scans every reachable root (sdmc / save / LOCAL / USB D–G) recursively.