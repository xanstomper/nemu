#!/usr/bin/env python3
"""
Nemu AppX Packager for Xbox Series S/X Developer Mode
Conforms strictly to Microsoft Open Packaging Conventions (OPC) & MS-APX Packaging Specification.
Generates compliant [Content_Types].xml, AppxBlockMap.xml (with SHA-256 block & full file hashes),
and orders footprint files per Windows AppX Deployment Service requirements.
"""

import os
import sys
import hashlib
import base64
import zipfile
from pathlib import Path
try:
    import pefile
except ImportError:
    pefile = None

BLOCK_SIZE = 65536  # 64 KiB per MS-APX spec

CONTENT_TYPES_XML = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
    '<Default Extension="png" ContentType="image/png"/>'
    '<Default Extension="xml" ContentType="application/vnd.ms-appx.manifest+xml"/>'
    '<Default Extension="exe" ContentType="application/x-msdownload"/>'
    '<Default Extension="dll" ContentType="application/x-msdownload"/>'
    '<Default Extension="winmd" ContentType="application/octet-stream"/>'
    '<Default Extension="keys" ContentType="application/octet-stream"/>'
    '<Default Extension="cer" ContentType="application/x-x509-ca-cert"/>'
    '<Default Extension="dat" ContentType="application/octet-stream"/>'
    '<Default Extension="bin" ContentType="application/octet-stream"/>'
    '<Override PartName="/AppxBlockMap.xml" ContentType="application/vnd.ms-appx.blockmap+xml"/>'
    '<Override PartName="/AppxSignature.p7x" ContentType="application/vnd.ms-appx.signature"/>'
    '</Types>\n'
)

def sha256_b64(data: bytes) -> str:
    return base64.b64encode(hashlib.sha256(data).digest()).decode('ascii')

def compute_block_map(files: list[tuple[str, bytes]]) -> str:
    """
    Generate AppxBlockMap.xml from list of (arcname, data).
    Conforms strictly to MS-APX Section 2.1:
    - Excludes [Content_Types].xml, AppxBlockMap.xml, AppxSignature.p7x
    - For uncompressed blocks: Size attribute MUST NOT be present on <Block>
    - Includes b4:FileHash ONLY for multi-block files (> 64 KiB), matching official Microsoft tools
    - Formats canonically without extra line breaks/indentation to strictly comply with AppX parsers
    """
    xml_parts = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n',
        '<BlockMap xmlns="http://schemas.microsoft.com/appx/2010/blockmap" xmlns:b4="http://schemas.microsoft.com/appx/2021/blockmap" IgnorableNamespaces="b4" HashMethod="http://www.w3.org/2001/04/xmlenc#sha256">'
    ]

    for arcname, data in files:
        name_win = arcname.replace('/', '\\')
        file_size = len(data)
        lfh_size = 30 + len(arcname.encode('utf-8'))

        xml_parts.append(f'<File Name="{name_win}" Size="{file_size}" LfhSize="{lfh_size}">')

        if file_size == 0:
            empty_hash = sha256_b64(b'')
            xml_parts.append(f'<Block Hash="{empty_hash}"/>')
        else:
            num_blocks = (file_size + BLOCK_SIZE - 1) // BLOCK_SIZE
            for offset in range(0, file_size, BLOCK_SIZE):
                chunk = data[offset:offset + BLOCK_SIZE]
                chunk_hash = sha256_b64(chunk)
                xml_parts.append(f'<Block Hash="{chunk_hash}"/>')
            if num_blocks > 1:
                full_file_hash = sha256_b64(data)
                xml_parts.append(f'<b4:FileHash Hash="{full_file_hash}"/>')

        xml_parts.append('</File>')

    xml_parts.append('</BlockMap>')
    return ''.join(xml_parts)

import shutil
import subprocess

def sign_appx(appx_path: Path, cert_path: Path, key_path: Path) -> bool:
    """
    Sign AppX package using osslsigncode to produce specification-compliant
    AppxSignature.p7x with Authenticode / SPC Indirect Data hashes.
    """
    osslsigncode = shutil.which("osslsigncode") or "/usr/bin/osslsigncode"
    if not os.path.exists(osslsigncode):
        print(f"[!] Warning: osslsigncode not found. AppX remains unsigned.")
        return False

    if not cert_path.exists() or not key_path.exists():
        print(f"[!] Warning: Certificate ({cert_path}) or Key ({key_path}) not found. Skipping signature.")
        return False

    print(f"[*] Digitally signing AppX package with {cert_path.name}...")
    signed_tmp = appx_path.with_suffix(".signed.tmp")
    
    cmd_sign = [
        osslsigncode, "sign",
        "-pem",
        "-certs", str(cert_path),
        "-key", str(key_path),
        "-in", str(appx_path),
        "-out", str(signed_tmp)
    ]
    
    res = subprocess.run(cmd_sign, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[!] Signing failed:\n{res.stderr}\n{res.stdout}")
        if signed_tmp.exists():
            signed_tmp.unlink()
        return False
        
    signed_tmp.replace(appx_path)
    print(f"[+] Package signed successfully: AppxSignature.p7x added.")

    # Verify complete ZIP container integrity
    with zipfile.ZipFile(appx_path, 'r') as test_zf:
        for info in test_zf.infolist():
            try:
                _ = test_zf.read(info.filename)
            except Exception as e:
                raise RuntimeError(f"Package ZIP corruption detected on {info.filename}: {e}")
    print("[+] All package files verified: 100% readable with valid CRC-32.")
    
    # Verify signature
    cmd_verify = [
        osslsigncode, "verify",
        "-CAfile", str(cert_path),
        "-in", str(appx_path)
    ]
    ver_res = subprocess.run(cmd_verify, capture_output=True, text=True)
    if ver_res.returncode == 0:
        print("[+] Signature integrity verified (BlockMap, ContentTypes, Data, Central Directory all OK).")
    else:
        print(f"[!] Signature verification warning:\n{ver_res.stderr}\n{ver_res.stdout}")
        
    return True

def ensure_uwp_pe_headers(file_path: Path):
    """
    Ensure PE binaries targeting Xbox Developer Mode have UWP AppContainer characteristics.
    Xbox kernel / AppX Deployment rejects binaries without IMAGE_DLLCHARACTERISTICS_APPCONTAINER (0x1000)
    or binaries with legacy subsystem versions with error 0x8007000B (ERROR_BAD_FORMAT).
    """
    if pefile is None:
        return
    try:
        pe = pefile.PE(str(file_path))
        modified = False
        # IMAGE_DLLCHARACTERISTICS_APPCONTAINER (0x1000)
        # IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE (0x8000)
        target_dll_char = pe.OPTIONAL_HEADER.DllCharacteristics | 0x1000 | 0x8000
        if pe.OPTIONAL_HEADER.DllCharacteristics != target_dll_char:
            pe.OPTIONAL_HEADER.DllCharacteristics = target_dll_char
            modified = True
        if pe.OPTIONAL_HEADER.MajorSubsystemVersion < 10:
            pe.OPTIONAL_HEADER.MajorSubsystemVersion = 10
            pe.OPTIONAL_HEADER.MinorSubsystemVersion = 0
            modified = True
        if pe.OPTIONAL_HEADER.MajorOperatingSystemVersion < 10:
            pe.OPTIONAL_HEADER.MajorOperatingSystemVersion = 10
            pe.OPTIONAL_HEADER.MinorOperatingSystemVersion = 0
            modified = True
        if pe.FILE_HEADER.NumberOfSymbols != 0:
            pe.FILE_HEADER.NumberOfSymbols = 0
            pe.FILE_HEADER.PointerToSymbolTable = 0
            modified = True
        if modified:
            pe.OPTIONAL_HEADER.CheckSum = pe.generate_checksum()
            pe.write(str(file_path))
            print(f"[+] UWP PE compliance applied to {file_path.name}: DllCharacteristics={hex(target_dll_char)}, Subsystem=10.0, CheckSum={hex(pe.OPTIONAL_HEADER.CheckSum)}")
        pe.close()
    except Exception as e:
        print(f"[!] Note: Could not process PE headers on {file_path.name}: {e}")

def pack_appx(staging_dir: Path, output_appx: Path, cert_path: Path | None = None, key_path: Path | None = None):
    print(f"[*] Packaging AppX from: {staging_dir}")
    print(f"[*] Output package:     {output_appx}")
    
    manifest_path = staging_dir / "AppxManifest.xml"
    exe_path = staging_dir / "Nemu.exe"
    if not manifest_path.exists():
        raise FileNotFoundError(f"Missing AppxManifest.xml in {staging_dir}")
    if not exe_path.exists():
        raise FileNotFoundError(f"Missing Nemu.exe in {staging_dir}")
        
    # Ensure all PE binaries have UWP AppContainer headers before packaging
    for root, _, files in os.walk(staging_dir):
        for f in files:
            p = Path(root) / f
            if p.suffix.lower() in ('.exe', '.dll'):
                ensure_uwp_pe_headers(p)
        
    payload_files: list[tuple[str, bytes]] = []
    manifest_data: tuple[str, bytes] = ("", b"")
    
    for root, _, files in os.walk(staging_dir):
        for f in sorted(files):
            file_path = Path(root) / f
            rel_path = file_path.relative_to(staging_dir).as_posix()
            
            # Exclude existing package metadata if re-running
            if rel_path in ("[Content_Types].xml", "AppxBlockMap.xml", "AppxSignature.p7x"):
                continue
                
            data = file_path.read_bytes()
            if rel_path == "AppxManifest.xml":
                manifest_data = (rel_path, data)
            else:
                payload_files.append((rel_path, data))
                
    # Sort payload files deterministically
    payload_files.sort(key=lambda x: x[0])
    
    # BlockMap hashes payload files + AppxManifest.xml
    # Per MS-APX, [Content_Types].xml is an OPC footprint file and MUST NOT be in BlockMap
    files_to_hash = payload_files + [manifest_data]
    blockmap_xml = compute_block_map(files_to_hash)
    blockmap_data = ("AppxBlockMap.xml", blockmap_xml.encode('utf-8'))
    content_types_data = ("[Content_Types].xml", CONTENT_TYPES_XML.encode('utf-8'))
    
    # Windows AppX Deployment Service ordering:
    # 1. Payload files
    # 2. AppxManifest.xml
    # 3. AppxBlockMap.xml
    # 4. [Content_Types].xml
    all_files = payload_files + [manifest_data, blockmap_data, content_types_data]
    
    output_appx.parent.mkdir(parents=True, exist_ok=True)
    if output_appx.exists():
        output_appx.unlink()
        
    with zipfile.ZipFile(output_appx, 'w', compression=zipfile.ZIP_STORED) as zf:
        for arcname, data in all_files:
            zinfo = zipfile.ZipInfo(arcname)
            zinfo.compress_type = zipfile.ZIP_STORED
            zinfo.date_time = (2026, 9, 12, 12, 0, 0)
            zinfo.external_attr = 0o644 << 16
            zinfo.create_system = 0  # Windows / MS-DOS
            zinfo.extract_version = 20  # PKZip 2.0
            zf.writestr(zinfo, data)
            print(f"  + Added: {arcname} ({len(data)} bytes, LFH={30 + len(arcname.encode('utf-8'))})")
            
    pkg_size = output_appx.stat().st_size
    print(f"\n[+] Successfully generated AppX container: {output_appx} ({pkg_size / 1024 / 1024:.2f} MB)")
    
    # Auto-sign if cert/key provided or default found
    if cert_path is None or key_path is None:
        root_dir = Path(__file__).resolve().parent.parent
        default_cert = root_dir / "packaging" / "xbox" / "NemuDev.cer"
        default_key = root_dir / "packaging" / "xbox" / "NemuDev.key"
        if default_cert.exists() and default_key.exists():
            cert_path = default_cert
            key_path = default_key

    if cert_path and key_path and cert_path.exists() and key_path.exists():
        sign_appx(output_appx, cert_path, key_path)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: make_appx.py <staging_dir> <output_appx> [cert_file] [key_file]")
        sys.exit(1)
    staging = Path(sys.argv[1])
    out_appx = Path(sys.argv[2])
    cert = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    key = Path(sys.argv[4]) if len(sys.argv) > 4 else None
    pack_appx(staging, out_appx, cert, key)


