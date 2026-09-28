#!/usr/bin/env bash
# ============================================================================
# NEMU Xbox one-shot bring-up: one command from source to running on console.
#
#   scripts/xbox_bringup.sh <XBOX_IP>            # package + deploy + headless QA
#   scripts/xbox_bringup.sh --package-only        # just build the AppX
#   scripts/xbox_bringup.sh --full <XBOX_IP>      # interactive: package, deploy,
#                                                  #  launch a real title, watch trace
#
# Prereqs: build-win toolchain already configured (MinGW + D3D12 headers),
#          console in Dev Mode with Device Portal enabled, on same LAN.
# ============================================================================
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTION="${1:---help}"

info() { echo "  [*] $*"; }
pass() { echo "  [+] $*"; }
fail() { echo >&2 "  [!!] $*"; exit 1; }

case "${ACTION}" in
    --package-only)
        "${ROOT_DIR}/scripts/package_xbox.sh" || fail "packaging failed"
        pass "AppX built: build-win/Nemulator_1.0.0.0_x64.appx"
        exit 0
        ;;
    --help|-h|"")
        cat <<'EOF'
NEMU Xbox bring-up — package, deploy, and boot-probe the emulator on a
Developer-Mode Xbox in a single command.

USAGE:
  scripts/xbox_bringup.sh <XBOX_IP> [NRO_NAME] [MAX_FRAMES]   package+deploy+QA
  scripts/xbox_bringup.sh --package-only                      build AppX only
  scripts/xbox_bringup.sh --full <XBOX_IP> [NRO_NAME]         interactive full test

ARGS:
  XBOX_IP    LAN address of the Dev Mode Xbox (Device Portal).
  NRO_NAME   .nro filename staged on E:\nemu\sdmc\ (default: linux-realboot-sample.nro)
  MAX_FRAMES frames for the boot probe (default 3; use 60 for a real title).

EXIT CODES (from scripts/qa_xbox.sh):
  0  deployed + D3D12 init clean + frames advanced -> BOOTED
  4  D3D12 init error detected on hardware (see build-win/qa_trace.log)
EOF
        exit 0
        ;;
    --full)
        IP="${2:?require <XBOX_IP> after --full}"
        NRO="${3:-linux-realboot-sample.nro}"
        "${ROOT_DIR}/scripts/package_xbox.sh" || fail "packaging failed"
        info "Packaged. Deploying + launching ${NRO} with 60 frames for a real boil."
        exec "${ROOT_DIR}/scripts/qa_xbox.sh" "${IP}" "${NRO}" 60
        ;;
    *)
        IP="${ACTION}"
        NRO="${2:-linux-realboot-sample.nro}"
        FRAMES="${3:-3}"
        echo "============================================================"
        echo "  NEMU Xbox bring-up  (${IP})  rom=${NRO} frames=${FRAMES}"
        echo "============================================================"
        # 1. Build the AppX
        info "[1/3] Packaging..."
        "${ROOT_DIR}/scripts/package_xbox.sh" || fail "packaging failed"
        pass "AppX ready."
        # 2. Deploy + headless boot probe via QA harness
        info "[2/3] Deploying + running headless boot probe..."
        "${ROOT_DIR}/scripts/qa_xbox.sh" "${IP}" "${NRO}" "${FRAMES}"
        rc=$?
        echo "============================================================"
        if [[ $rc -eq 0 ]]; then
            pass "[3/3] BOOTED on hardware. Stack verified end-to-end."
        else
            fail "[3/3] Boot probe did not pass cleanly (exit ${rc}). See build-win/qa_trace.log"
        fi
        exit $rc
        ;;
esac