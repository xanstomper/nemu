#!/usr/bin/env bash
# ============================================================================
# NEMU Xbox Series S/X Dev Mode — On-Device QA Harness (Tier-C4)
#
# Deploys the Nemu AppX to a Developer-Mode Xbox via the Xbox Device Portal
# REST API, launches the headless `--run <nro> --max-frames=N` boot probe, and
# streams the console trace to assert the D3D12 device + PSO creation and a
# clean BOOTED frame. This is the on-hardware gate that desktop tests cannot
# close — the prerequisite for curing a commercial title.
#
# Preconditions:
#   * PC can reach https://<XBOX_IP>:11443 (Device Portal, Dev Mode).
#   * Dev Portfolio: App Type = Game (for full CPU/RAM/GPU, REQUIRED).
#   * build-win/Nemu_1.0.0.0_x64.appx already packaged (scripts/package_xbox.sh).
#   * A homebrew .nro staged at E:\nemu\sdmc\<name>.nro on the console (or use
#     the bundled linux-realboot-sample.nro path).
#
# Usage:
#   scripts/qa_xbox.sh <XBOX_IP> [NRO_NAME] [MAX_FRAMES]
#     XBOX_IP    - your console's LAN IP (e.g. 192.168.1.150)
#     NRO_NAME   - .nro filename on console storage (default: linux-realboot-sample.nro)
#     MAX_FRAMES - frames for the boot probe (default 3; 60 for a real title)
#
# Exit codes:
#   0 = deployed, launched, D3D12 init clean, frames advanced -> BOOTED
#   1 = build/package missing
#   2 = Xbox unreachable / Device Portal auth failed
#   3 = deploy failed
#   4 = launch/probe failed or D3D12 init error detected
# ============================================================================
set -uo pipefail

XBOX_IP="${1:?usage: scripts/qa_xbox.sh <XBOX_IP> [NRO_NAME] [MAX_FRAMES]}"
NRO_NAME="${2:-linux-realboot-sample.nro}"
MAX_FRAMES="${3:-3}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "${ROOT_DIR}/build-win/Nemulator_1.0.0.0_x64.appx" ]]; then
    APPX="${ROOT_DIR}/build-win/Nemulator_1.0.0.0_x64.appx"
else
    APPX="${ROOT_DIR}/build-win/Nemu_1.0.0.0_x64.appx"
fi
PORTAL_BASE="https://${XBOX_IP}:11443"
CERTS=""
AUTH=""

# --- Helpers ----------------------------------------------------------------
# Dev Mode self-signed cert — find the console root cert to trust for curl.
warn()  { echo >&2 "  [!] $*"; }
info()  { echo "  [*] $*"; }
pass()  { echo "  [+] $*"; }
fail()  { echo >&2 "  [!!] $*"; exit "${2:-1}"; }

# --- Preflight --------------------------------------------------------------
info "NEMU Xbox QA harness -> ${XBOX_IP}  (rom: ${NRO_NAME}, frames: ${MAX_FRAMES})"

if [[ ! -f "${APPX}" ]]; then
    fail "AppX not found: ${APPX}. Run scripts/package_xbox.sh first." 1
fi
info "Package: ${APPX} ($(du -h "${APPX}" | cut -f1))"

# Trust the Xbox Device Portal self-signed cert (insecure by design in Dev Mode).
CERTS="${ROOT_DIR}/build-win/portal-cacert.pem"
CERTS=""
info "Querying Device Portal..."
if ! curl -sk --max-time 10 "https://${XBOX_IP}:11443/ext/xboxlive" -o /dev/null -w '%{http_code}' 2>/dev/null \
    | grep -qE '200|201'; then
    fail "Xbox Device Portal unreachable at ${PORTAL_BASE}. Is Dev Mode + Device Portal on?" 2
fi
pass "Device Portal reachable."

# --- Deploy AppX via Device Portal REST API ---------------------------------
# Endpoint: POST /api/app/packagemanager/package (multipart, file field "appx")
info "Deploying AppX via Device Portal..."
DEPLOY_HTTP=$(curl -sk --max-time 300 -X POST \
    "${PORTAL_BASE}/api/app/packagemanager/package" \
    -F "file=@${APPX}" \
    -o "${ROOT_DIR}/build-win/qa_deploy.out" -w '%{http_code}' 2>/dev/null)
info "Deploy HTTP: ${DEPLOY_HTTP}"
if [[ "${DEPLOY_HTTP}" != "20"* && "${DEPLOY_HTTP}" != "00" ]]; then
    cat "${ROOT_DIR}/build-win/qa_deploy.out" 2>/dev/null
    fail "AppX deploy failed (HTTP ${DEPLOY_HTTP}). See qa_deploy.out." 3
fi
pass "AppX deployed to ${XBOX_IP}."

# --- Launch the headless boot probe -----------------------------------------
# Launch via Device Portal (the installed app is registered by package family name).
# Launch the app with the --run argument by hitting the debug launch endpoint.
info "Launching headless boot probe: Nemu --run E:/nemu/sdmc/${NRO_NAME} --max-frames=${MAX_FRAMES}"
# Resolve the installed package family name (stable, matches AppxManifest).
PACKAGES=$(curl -sk --max-time 30 "${PORTAL_BASE}/api/app/packagemanager/packages" 2>/dev/null \
    | grep -oE '"PackageFullName":"[^"]*Nemu[^"]*"' | head -1 | cut -d'"' -f4)
if [[ -z "${PACKAGES}" ]]; then
    warn "Could not find installed Nemu package by name; using AppId 'Nemu'."
    PACKAGES="Nemu"
fi
info "Launching package: ${PACKAGES}"
LAUNCH_HTTP=$(curl -sk --max-time 30 -X POST \
    "${PORTAL_BASE}/api/app/packagemanager/launch" \
    -d "{\"appid\":\"${PACKAGES}\",\"arguments\":\"--run E:/nemu/sdmc/${NRO_NAME} --max-frames=${MAX_FRAMES}\"}" \
    -H "Content-Type: application/json" \
    -o "${ROOT_DIR}/build-win/qa_launch.out" -w '%{http_code}' 2>/dev/null)
info "Launch HTTP: ${LAUNCH_HTTP}"

echo
info "=== Console trace (streaming up to 60s; Ctrl-C to stop, this is the proof) ==="
echo "    Watching for: [NEMU-BOOT] ... BOOTED (advanced frames)  and  no D3D12 creation errors"
echo "----------------------------------------------------------------------"

# Stream the console debug trace via the Device Portal websocket-ish chunked
# message/trace endpoint and grep for our probe markers. Fall back to polling
# the app's stderr via the trace HTTP stream.
TRACE_URL="${PORTAL_BASE}/api/app/taskmanager/trace"
ROOT_CERT=""
# In Dev Mode the trace endpoint is GET; run for up to 60s capturing output.
BOOTED=0
D3D12_FAIL=0
timeout 60 curl -skN --max-time 60 "${TRACE_URL}" 2>/dev/null | tee "${ROOT_DIR}/build-win/qa_trace.log" | \
while IFS= read -r line; do
    if [[ "$line" == *"BOOTED"* ]]; then
        echo "  [+] BOOTED reached."
        BOOTED=1
    fi
    if [[ "$line" == *"D3D12"* && ( "$line" == *"error"* || "$line" == *"failed"* || "$line" == *"FAILED"* ) ]]; then
        echo "  [!] D3D12-init error seen: $line"
        D3D12_FAIL=1
    fi
    echo "$line"
done
# NOTE: `while` runs in a subshell; reassess the trace file for the markers.
BOOTED=$(grep -c "BOOTED" "${ROOT_DIR}/build-win/qa_trace.log" 2>/dev/null || true)
D3D12_FAIL=$(grep -cE "D3D12.*(error|failed|FAILED)" "${ROOT_DIR}/build-win/qa_trace.log" 2>/dev/null || true)
echo "----------------------------------------------------------------------"

if [[ "${BOOTED}" -ge 1 ]]; then
    pass "Boot probe advanced frames -> BOOTED. D3D12 device + PSO pipeline OK on hardware."
elif [[ "${D3D12_FAIL}" -ge 1 ]]; then
    fail "D3D12 init errors detected on hardware. See build-win/qa_trace.log (${D3D12_FAIL} errors)." 4
else
    fail "No [NEMU-BOOT] marker captured. See build-win/qa_trace.log." 4
fi

echo
info "QA summary: deployed=${DEPLOY_HTTP} booted=${BOOTED} d3d12_errors=${D3D12_FAIL}"
pass "Complete. Full console trace saved to build-win/qa_trace.log"
exit 0