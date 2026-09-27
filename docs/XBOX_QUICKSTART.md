# NEMU on Xbox — 3-command Quickstart

From a cloned repo to the emulator running on your Developer-Mode Xbox.

## Prereqs (one-time)
- **Xbox**: Dev Mode enabled (Dev Home app → Developer Mode) + Device Portal ON.
- **PC**: MinGW + D3D12 headers configured for `build-win/`, same LAN as the Xbox.
- **Keys** (for retail titles): `keys/prod.keys` + `keys/title.keys` in the repo root.

## The 3 commands

```bash
# 1. Configure/verify the Windows cross-toolchain once
#    (already done if build-win/ builds — otherwise see BUILDING.md)

# 2. One-shot bring-up: package + deploy + headless boot-probe your console
scripts/xbox_bringup.sh 192.168.1.150

# 3. To test a real title (retail NSO/NCA), stage it then run with more frames:
scripts/xbox_bringup.sh --full 192.168.1.150 mytitle.nso   # 60-frame boil
```

## What each piece does

| Command/File | Purpose |
|---|---|
| `scripts/xbox_bringup.sh` | orchestrator: package → deploy → probe → PASS/FAIL verdict |
| `scripts/package_xbox.sh` | builds the AppX (bundles Nemu.exe + prod.keys + title.keys + assets) |
| `scripts/qa_xbox.sh` | deploy via Device Portal + run `--run` probe + stream trace for `BOOTED`/D3D12 errors |
| `docs/XBOX_DEPLOYMENT_GUIDE.md` | manual Device Portal path + full controller bindings |
| `docs/BOOT_READINESS_AUDIT.md` | full load→translate→render chain map + expected on-hardware results |

## Critical console setting (do not skip)
In Xbox Device Portal → Apps → Nemu → options:
**App Type = Game** (full Zen 2 cores + expanded RAM/GPU). Without it the
app is restricted to 2 cores / ~2 GB RAM and won't boot games properly.

## Expected result
Exit `0` = deployed + D3D12 init clean + `[NEMU-BOOT] → BOOTED (advanced frames)`.
That is the on-hardware proof the whole stack works. Full console trace is
saved to `build-win/qa_trace.log`.

## Troubleshooting
- **HTTP 4 on probe** (exit code 4): D3D12 init error on hardware — the QA
  harness flags the exact failing lines. Cite them when reporting.
- **Black screen**: App Type not `Game`, or the title's RomFS/key isn't loaded.
- **No BOOTED marker**: wrong `E:\nemu\sdmc\` path or the app's working dir
  (use `--full` and watch the live trace).