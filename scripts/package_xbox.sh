#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-win"
STAGING_DIR="${BUILD_DIR}/package_staging"
OUTPUT_APPX="${BUILD_DIR}/Nemu_1.0.0.0_x64.appx"

echo "=========================================================="
echo "  NEMU: Xbox Series S/X Developer Mode Packaging Pipeline "
echo "=========================================================="

# 1. Ensure Windows PE32+ build is complete
echo "[1/4] Building Windows / Xbox Release binary..."
ninja -C "${BUILD_DIR}" -j2 Nemu.exe

if [[ ! -f "${BUILD_DIR}/bin/Nemu.exe" ]]; then
    echo "Error: ${BUILD_DIR}/bin/Nemu.exe not found!"
    exit 1
fi

# 2. Setup Staging Area
echo "[2/4] Assembling APPX staging directory..."
rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}/Assets"

# Copy binary
cp "${BUILD_DIR}/bin/Nemu.exe" "${STAGING_DIR}/Nemu.exe"

# Copy AppxManifest
cp "${ROOT_DIR}/packaging/xbox/AppxManifest.xml" "${STAGING_DIR}/AppxManifest.xml"

# Copy visual assets
cp -r "${ROOT_DIR}/packaging/xbox/Assets/"* "${STAGING_DIR}/Assets/"

# Copy cryptographic keys into package
if [[ -d "${ROOT_DIR}/keys" ]]; then
    echo "  -> Bundling production keys into AppX package..."
    mkdir -p "${STAGING_DIR}/keys"
    cp -r "${ROOT_DIR}/keys/"* "${STAGING_DIR}/keys/"
    cp "${ROOT_DIR}/keys/prod.keys" "${STAGING_DIR}/prod.keys" || true
    cp "${ROOT_DIR}/keys/title.keys" "${STAGING_DIR}/title.keys" || true
fi

# 3. Create Valid OPC/AppX Package Container
echo "[3/4] Building specification-compliant AppX package container (with BlockMap & Content_Types)..."
python3 "${ROOT_DIR}/scripts/make_appx.py" "${STAGING_DIR}" "${OUTPUT_APPX}"

# Copy certificate next to appx for Xbox Device Portal installation
cp "${ROOT_DIR}/packaging/xbox/NemuDev.cer" "${BUILD_DIR}/NemuDev.cer"
cp "${ROOT_DIR}/packaging/xbox/NemuDev.pfx" "${BUILD_DIR}/NemuDev.pfx"

# Also populate release directory
mkdir -p "${ROOT_DIR}/packaging/xbox/release"
cp "${OUTPUT_APPX}" "${ROOT_DIR}/packaging/xbox/release/"
cp "${ROOT_DIR}/packaging/xbox/NemuDev.cer" "${ROOT_DIR}/packaging/xbox/release/"
cp "${ROOT_DIR}/packaging/xbox/NemuDev.pfx" "${ROOT_DIR}/packaging/xbox/release/"

# 4. Verify & Report
echo "[4/4] Package Verification:"
APPX_SIZE=$(du -h "${OUTPUT_APPX}" | cut -f1)
echo "  -> Target Package: ${OUTPUT_APPX}"
echo "  -> Package Size:   ${APPX_SIZE}"
echo "  -> Certificate:    ${BUILD_DIR}/NemuDev.cer"
echo ""
echo "Package contents:"
unzip -l "${OUTPUT_APPX}"
echo ""
echo "=========================================================="
echo "  APPX PACKAGE & CERTIFICATE GENERATED SUCCESSFULLY!     "
echo "=========================================================="
echo "  Deployment Option A (Standard Signed AppX):             "
echo "  1. Open Xbox Device Portal (https://<xbox-ip>:11443)    "
echo "  2. Under 'Install app', choose 'Nemu_1.0.0.0_x64.appx'  "
echo "  3. Click Next, select certificate 'NemuDev.cer'         "
echo "  4. Click Start / Deploy                                 "
echo "                                                          "
echo "  Deployment Option B (Instant Loose Folder Deploy):      "
echo "  1. In Xbox Device Portal, select 'Deploy loose folder'  "
echo "  2. Choose folder: '${STAGING_DIR}'                      "
echo "  3. Deploys instantly without signature requirements!    "
echo "=========================================================="

