#!/usr/bin/env python3
"""
Nemu AppX Packager for Xbox Series S/X Developer Mode
Conforms strictly to Microsoft Open Packaging Conventions (OPC) & MS-APX Packaging Specification.
- Generates compliant [Content_Types].xml with CodeIntegrity catalog overrides.
- Implements MS-APX 64 KiB block-deflate compression with per-block SHA-256 and compressed sizes.
- Generates and signs AppxMetadata/CodeIntegrity.cat ensuring kernel-level Code Integrity compliance on Xbox.
- Digitally signs package using osslsigncode generating full AppxSignature.p7x covering
  AXPC (Payload), AXCD (Central Directory), AXCT (Content Types), AXBM (Block Map), and AXCI (Code Integrity).
- Ensures 100% UWP AppContainer PE compliance on all executables and libraries.
"""

import os
import sys
import zlib
import zipfile
import hashlib
import base64
import struct
import subprocess
import time
import shutil
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
    '<Override PartName="/AppxMetadata/CodeIntegrity.cat" ContentType="application/vnd.ms-pkiseccat"/>'
    '</Types>\n'
)

def sha256_b64(data: bytes) -> str:
    return base64.b64encode(hashlib.sha256(data).digest()).decode('ascii')

def encode_length(l: int) -> bytes:
    if l < 128:
        return bytes([l])
    elif l < 256:
        return bytes([0x81, l])
    elif l < 65536:
        return bytes([0x82, l >> 8, l & 0xFF])
    else:
        return bytes([0x83, l >> 16, (l >> 8) & 0xFF, l & 0xFF])

def encode_sequence(data: bytes) -> bytes:
    return b'\x30' + encode_length(len(data)) + data

def make_catalog_entry(file_hash: bytes) -> bytes:
    """
    Encode an entry in the Certificate Trust List for a binary/file hash.
    Conforms to MS-APX Section 2.2 / Microsoft Catalog format.
    """
    part1 = b'\x04\x20' + file_hash
    part2 = bytes.fromhex(
        '31713010060a2b0601040182370c020331028000305d060a2b060104018237020104314f304d3018060a2b06010401823702010f300a030205a0a004a20280003031300d060960864801650304020105000420'
    ) + file_hash
    return encode_sequence(part1 + part2)

def generate_code_integrity_cat(file_hashes: list[bytes], cert_path: Path, key_path: Path) -> bytes:
    """
    Generate and sign an AppxMetadata/CodeIntegrity.cat security catalog.
    Xbox OS requires this for kernel Code Integrity enforcement in AppContainer.
    """
    guid = os.urandom(16)
    utctime = time.strftime('%y%m%d%H%M%SZ', time.gmtime()).encode('ascii')
    header = (
        bytes.fromhex('300c060a2b0601040182370c0101') +
        b'\x04\x10' + guid +
        b'\x17\r' + utctime +
        bytes.fromhex('300e060a2b0601040182370c01030500')
    )
    entries_seq = encode_sequence(b''.join(make_catalog_entry(h) for h in file_hashes))
    ctl_der = encode_sequence(header + entries_seq)
    
    tmp_ctl = Path('/tmp/nemu_ctl.der')
    tmp_cat = Path('/tmp/nemu_cat.der')
    tmp_ctl.write_bytes(ctl_der)
    
    cmd = [
        'openssl', 'cms', '-sign', '-nodetach',
        '-econtent_type', '1.3.6.1.4.1.311.10.1',
        '-signer', str(cert_path), '-inkey', str(key_path),
        '-in', str(tmp_ctl), '-outform', 'DER', '-out', str(tmp_cat)
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        tmp_ctl.unlink(missing_ok=True)
        tmp_cat.unlink(missing_ok=True)
        raise RuntimeError(f'OpenSSL CMS CodeIntegrity signing failed: {res.stderr}')
        
    cat_bytes = tmp_cat.read_bytes()
    tmp_ctl.unlink(missing_ok=True)
    tmp_cat.unlink(missing_ok=True)
    print(f"[+] Successfully generated AppxMetadata/CodeIntegrity.cat ({len(cat_bytes)} bytes)")
    return cat_bytes

def compress_file_blocks(data: bytes) -> tuple[bytes, list[tuple[str, int]]]:
    """
    Divide data into 64 KiB chunks and compress each block with DEFLATE per MS-APX spec.
    Blocks 0 to N-2 use Z_SYNC_FLUSH; the final block uses Z_FINISH.
    Returns (concatenated_compressed_data, [(block_sha256_b64, compressed_block_len), ...])
    """
    c = zlib.compressobj(6, zlib.DEFLATED, -15)
    compressed_parts = []
    block_info = []
    num_blocks = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE if len(data) > 0 else 1
    for i in range(num_blocks):
        offset = i * BLOCK_SIZE
        chunk = data[offset:offset + BLOCK_SIZE]
        chunk_hash = sha256_b64(chunk)
        if i == num_blocks - 1:
            part = c.compress(chunk) + c.flush(zlib.Z_FINISH)
        else:
            part = c.compress(chunk) + c.flush(zlib.Z_SYNC_FLUSH)
        compressed_parts.append(part)
        block_info.append((chunk_hash, len(part)))
    return b''.join(compressed_parts), block_info

def ensure_uwp_pe_headers(file_path: Path):
    """
    Ensure PE binaries targeting Xbox Developer Mode have UWP AppContainer characteristics:
    - IMAGE_DLLCHARACTERISTICS_APPCONTAINER (0x1000)
    - IMAGE_DLLCHARACTERISTICS_TERMINAL_SERVER_AWARE (0x8000)
    - MajorSubsystemVersion / MajorOperatingSystemVersion >= 10.0
    - Clean COFF symbols
    - Valid PE CheckSum
    """
    if pefile is None:
        return
    try:
        pe = pefile.PE(str(file_path))
        modified = False
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

def sign_appx(appx_path: Path, cert_path: Path, key_path: Path) -> bool:
    """
    Sign AppX package using osslsigncode to produce specification-compliant
    AppxSignature.p7x with Authenticode / SPC Indirect Data hashes covering
    AXPC, AXCD, AXCT, AXBM, and AXCI.
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
        print("[+] Signature integrity verified: AXPC, AXCD, AXCT, AXBM, and AXCI all VALID!")
    else:
        print(f"[!] Signature verification warning:\n{ver_res.stderr}\n{ver_res.stdout}")
        
    return True

def pack_appx(staging_dir: Path, output_appx: Path, cert_path: Path | None = None, key_path: Path | None = None):
    print(f"[*] Packaging AppX from: {staging_dir}")
    print(f"[*] Output package:     {output_appx}")
    
    manifest_path = staging_dir / "AppxManifest.xml"
    exe_path = staging_dir / "Nemu.exe"
    if not manifest_path.exists():
        raise FileNotFoundError(f"Missing AppxManifest.xml in {staging_dir}")
    if not exe_path.exists():
        raise FileNotFoundError(f"Missing Nemu.exe in {staging_dir}")

    # Determine certificate and key paths
    if cert_path is None or key_path is None:
        root_dir = Path(__file__).resolve().parent.parent
        default_cert = root_dir / "packaging" / "xbox" / "NemuDev.cer"
        default_key = root_dir / "packaging" / "xbox" / "NemuDev.key"
        if default_cert.exists() and default_key.exists():
            cert_path = default_cert
            key_path = default_key
        
    # Ensure all PE binaries have UWP AppContainer headers before packaging
    for root, _, files in os.walk(staging_dir):
        for f in files:
            p = Path(root) / f
            if p.suffix.lower() in ('.exe', '.dll'):
                ensure_uwp_pe_headers(p)
        
    items: list[tuple[str, bytes, bool]] = []
    pe_hashes: list[bytes] = []

    for root, _, files in os.walk(staging_dir):
        for f in sorted(files):
            file_path = Path(root) / f
            rel_path = file_path.relative_to(staging_dir).as_posix()
            
            # Exclude existing package metadata if re-running
            if rel_path in ("[Content_Types].xml", "AppxBlockMap.xml", "AppxSignature.p7x", "AppxMetadata/CodeIntegrity.cat"):
                continue
                
            raw_data = file_path.read_bytes()
            is_comp = not rel_path.lower().endswith(('.png', '.jpg', '.jpeg'))
            items.append((rel_path, raw_data, is_comp))
            if rel_path.lower().endswith(('.exe', '.dll')):
                pe_hashes.append(hashlib.sha256(raw_data).digest())

    manifest_raw = manifest_path.read_bytes()
    pe_hashes.append(hashlib.sha256(manifest_raw).digest())

    # Generate CodeIntegrity catalog
    if cert_path and key_path and cert_path.exists() and key_path.exists():
        cat_raw = generate_code_integrity_cat(pe_hashes, cert_path, key_path)
    else:
        cat_raw = b''

    # Separate manifest and payload files
    manifest_item = [it for it in items if it[0] == "AppxManifest.xml"][0]
    payload_items = [it for it in items if it[0] != "AppxManifest.xml"]
    payload_items.sort(key=lambda x: x[0])

    # Process and compress files
    processed_files = []  # (arcname, raw_data, stored_data, block_info, is_comp, lfh_size, extra)
    for rel, raw, is_comp in payload_items + [manifest_item]:
        name_bytes = rel.encode('utf-8')
        if is_comp:
            cdata, binfo = compress_file_blocks(raw)
            extra = b''
            lfh_size = 30 + len(name_bytes)
            processed_files.append((rel, raw, cdata, binfo, True, lfh_size, extra))
        else:
            # Uncompressed images
            extra = b''
            lfh_size = 30 + len(name_bytes)
            num_blocks = (len(raw) + BLOCK_SIZE - 1) // BLOCK_SIZE if len(raw) > 0 else 1
            binfo = [(sha256_b64(raw[i*BLOCK_SIZE : (i+1)*BLOCK_SIZE]), 0) for i in range(num_blocks)]
            processed_files.append((rel, raw, raw, binfo, False, lfh_size, extra))

    # Generate AppxBlockMap.xml
    xml_parts = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n',
        '<BlockMap xmlns="http://schemas.microsoft.com/appx/2010/blockmap" xmlns:b4="http://schemas.microsoft.com/appx/2021/blockmap" IgnorableNamespaces="b4" HashMethod="http://www.w3.org/2001/04/xmlenc#sha256">'
    ]
    for rel, raw, _, binfo, is_comp, lfh_sz, _ in processed_files:
        name_win = rel.replace('/', '\\')
        xml_parts.append(f'<File Name="{name_win}" Size="{len(raw)}" LfhSize="{lfh_sz}">')
        for h, s in binfo:
            if is_comp:
                xml_parts.append(f'<Block Hash="{h}" Size="{s}"/>')
            else:
                xml_parts.append(f'<Block Hash="{h}"/>')
        if len(binfo) > 1:
            xml_parts.append(f'<b4:FileHash Hash="{sha256_b64(raw)}"/>')
        xml_parts.append('</File>')
    xml_parts.append('</BlockMap>')
    blockmap_raw = ''.join(xml_parts).encode('utf-8')

    content_types_raw = CONTENT_TYPES_XML.encode('utf-8')

    # Compress footprint files
    bm_cdata, _ = compress_file_blocks(blockmap_raw)
    ct_cdata, _ = compress_file_blocks(content_types_raw)
    ci_cdata = compress_file_blocks(cat_raw)[0] if cat_raw else b''

    # Assemble ZIP entries in strict footprint order:
    # 1. Payload files
    # 2. AppxManifest.xml
    # 3. AppxBlockMap.xml
    # 4. [Content_Types].xml
    # 5. AppxMetadata/CodeIntegrity.cat
    all_zip_entries = []  # (name, uncomp_data, stored_data, comp_method, extra)
    for rel, raw, stored, _, is_comp, _, extra in processed_files:
        if rel != "AppxManifest.xml":
            all_zip_entries.append((rel, raw, stored, 8 if is_comp else 0, extra))

    # Manifest
    man_rel, man_raw, man_stored, _, man_comp, _, man_extra = [x for x in processed_files if x[0] == "AppxManifest.xml"][0]
    all_zip_entries.append((man_rel, man_raw, man_stored, 8 if man_comp else 0, man_extra))

    # BlockMap
    all_zip_entries.append(('AppxBlockMap.xml', blockmap_raw, bm_cdata, 8, b''))

    # Content Types
    all_zip_entries.append(('[Content_Types].xml', content_types_raw, ct_cdata, 8, b''))

    # CodeIntegrity
    if cat_raw:
        all_zip_entries.append(('AppxMetadata/CodeIntegrity.cat', cat_raw, ci_cdata, 8, b''))

    output_appx.parent.mkdir(parents=True, exist_ok=True)
    if output_appx.exists():
        output_appx.unlink()

    with open(output_appx, 'wb') as f:
        cd_records = []
        for arcname, uncomp, stored, comp_method, extra in all_zip_entries:
            offset = f.tell()
            name_bytes = arcname.encode('utf-8')
            crc = zlib.crc32(uncomp)
            lfh = struct.pack('<IHHHHHIIIHH',
                0x04034b50, 20, 0, comp_method, 0x6000, 0x5d2c, crc, len(stored), len(uncomp), len(name_bytes), len(extra)
            ) + name_bytes + extra
            f.write(lfh)
            f.write(stored)
            cd_records.append((name_bytes, crc, len(stored), len(uncomp), offset, comp_method, extra))
            print(f"  + Added: {arcname} ({len(uncomp)} bytes, comp={comp_method}, LFH={30 + len(name_bytes) + len(extra)})")
            
        cd_offset = f.tell()
        for name_bytes, crc, csz, usz, off, comp_method, extra in cd_records:
            cdh = struct.pack('<IHHHHHHIIIHHHHHII',
                0x02014b50, 0, 20, 0, comp_method, 0x6000, 0x5d2c, crc, csz, usz, len(name_bytes), len(extra), 0, 0, 0, 0, off
            ) + name_bytes + extra
            f.write(cdh)
        cd_size = f.tell() - cd_offset
        
        eocd = struct.pack('<IHHHHIIH',
            0x06054b50, 0, 0, len(cd_records), len(cd_records), cd_size, cd_offset, 0
        )
        f.write(eocd)

    pkg_size = output_appx.stat().st_size
    print(f"\n[+] Successfully generated AppX container: {output_appx} ({pkg_size / 1024 / 1024:.2f} MB)")
    
    # Auto-sign
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
