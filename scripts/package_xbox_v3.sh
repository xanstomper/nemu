#!/usr/bin/env bash
# Nemu Xbox APPX packager (v3) — PURE LINUX, using real tooling:
#   1. Pack  : native Microsoft MSIX Packaging SDK (makemsix) — produces a valid
#              OPC/APPX container (fixes the old hand-rolled writer's 0x8007000B).
#   2. Sign  : osslsigncode (git master, >= 2.15) which has native APPX/MSIX
#              signing — produces a valid AppxSignature.p7x on Linux.
#   3. Verify: osslsigncode verify — confirms blockmap/content-types digests.
#
# No Windows/signtool/makeappx.exe needed.
#
# Usage: scripts/package_xbox_v3.sh [output.appx]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAKEMSIX="${MAKEMSIX:-/tmp/msix-packaging/build/bin/makemsix}"
OSSLC="${OSSLC:-/tmp/osslsigncode-git/build/osslsigncode}"
STAGING="$ROOT/packaging/xbox/staging"
PFX="$ROOT/packaging/xbox/NemuDev.pfx"
PFX_PASS="${NEMU_PFX_PASS:-nemu}"
OUT="${1:-$ROOT/build-win/Nemu_1.0.0.0_x64.appx}"
EXE_SRC="$ROOT/build-win/bin/Nemu.exe"
UNSIGNED="$(dirname "$OUT")/Nemu_unsigned.appx"

echo "== Nemu Xbox APPX packager v3 (pure Linux) =="
for t in "$MAKEMSIX" "$OSSLC"; do [[ -x "$t" ]] || { echo "ERROR: missing $t"; exit 1; }; done
[[ -f "$PFX" ]] || { echo "ERROR: missing $PFX"; exit 1; }
[[ -f "$EXE_SRC" ]] || { echo "ERROR: missing $EXE_SRC"; exit 1; }

# --- Staging (NEVER include key files) ---
echo "[1/4] Staging clean payload (no .keys)..."
rm -rf "$STAGING"; mkdir -p "$STAGING/Assets"
cp "$ROOT/packaging/xbox/AppxManifest.xml" "$STAGING/"
cp "$EXE_SRC" "$STAGING/Nemu.exe"
cp "$ROOT"/packaging/xbox/Assets/*.png "$STAGING/Assets/"
rm -f "$STAGING"/*.keys "$STAGING"/keys 2>/dev/null || true

# --- UWP PE compliance ---
echo "[2/4] Applying UWP PE characteristics to Nemu.exe..."
python3 - "$STAGING/Nemu.exe" <<'PYEOF'
import struct,sys
p=sys.argv[1]; d=bytearray(open(p,'rb').read())
pe=struct.unpack_from('<I',d,0x3C)[0]; o=pe+24
dll=o+70; old=struct.unpack_from('<H',d,dll)[0]
struct.pack_into('<H',d,dll,old|0x1000|0x8000)  # APPCONTAINER|TERMINAL_SERVER_AWARE
for off in (o+40,o+48):
    if struct.unpack_from('<H',d,off)[0]<10: struct.pack_into('<HH',d,off,10,0)
s=o+64; struct.pack_into('<I',d,s,0)
if len(d)%2: d.append(0)
t=0
for i in range(0,len(d)-1,2): t=(t+struct.unpack_from('<H',d,i)[0])&0xFFFFFFFF
t=((t&0xFFFF)+(t>>16))&0xFFFF; t=(t+(t>>16))&0xFFFF
struct.pack_into('<I',d,s,(t+len(d))&0xFFFFFFFF)
open(p,'wb').write(d)
print("    PE ok: DllChars 0x%04x->0x%04x"%(old,old|0x1000|0x8000))
PYEOF

# --- Pack ---
echo "[3/4] Packing with Microsoft msix-packaging (makemsix)..."
rm -f "$UNSIGNED"
"$MAKEMSIX" pack -d "$STAGING" -p "$UNSIGNED" 2>&1 | grep -av '^Microsoft\|Copyright\|^$' || true
[[ -f "$UNSIGNED" ]] || { echo "ERROR: makemsix produced no output"; exit 1; }

# --- Sign ---
echo "[4/4] Signing with osslsigncode APPX support..."
"$OSSLC" sign -pkcs12 "$PFX" -pass "$PFX_PASS" \
    -in "$UNSIGNED" -out "$OUT" 2>&1 | grep -aiE "package|succeeded|error|appx" | head -4

# --- Verify ---
echo "Verifying signature..."
"$OSSLC" verify -CAfile "$ROOT/packaging/xbox/NemuDev.cer" -in "$OUT" 2>&1 \
    | grep -aiE "signature verification|verified signatures|ok|error" | head -4

echo
echo "== SUCCESS: $OUT =="
ls -la "$OUT"
echo
echo "Deploy: Xbox Device Portal -> (Settings/Developer) -> Add Signing Certificate"
echo "        -> upload $ROOT/packaging/xbox/NemuDev.cer"
echo "        then deploy  $OUT"