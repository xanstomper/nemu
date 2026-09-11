# Nemu Xbox Build & Deployment Guide

## 1. Overview

This document outlines the reproducible build pipeline for compiling Nemu and generating an Xbox Developer Mode sideloadable package (`.appx` / `.msix`).

---

## 2. Toolchain Requirements

### Linux Build Host (Cross-Compilation Path)
* **CMake:** >= 3.22 (Host installed: 3.28.3)
* **Ninja:** >= 1.10 (Host installed: 1.13.0)
* **C++ Cross-Compiler:** `x86_64-w64-mingw32-g++` (GCC 13+) or `clang` targeting `x86_64-pc-windows-msvc`
* **Archivers:** `zip` / `7z`
* **Certificate Tooling:** `openssl` for self-signed development certificates

### Windows / MSVC Build Host (Alternative Path)
* **Visual Studio 2022:** Community or Professional with "Universal Windows Platform development" and "Desktop development with C++"
* **Windows 10/11 SDK:** 10.0.22621.0 or higher
* **DirectX Shader Compiler:** Included in Windows SDK

---

## 3. CMake Cross-Compilation Configuration

To configure Nemu for Windows/Xbox from a Linux host:

```bash
mkdir -p build-xbox && cd build-xbox
cmake .. \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEMU_BUILD_XBOX=ON \
    -DNEMU_ENABLE_TESTS=OFF \
    -G Ninja

ninja
```

---

## 4. Packaging into APPX for Xbox Developer Mode

An Xbox Developer Mode application package (`.appx`) is a standardized ZIP container adhering to the Open Packaging Conventions (OPC) and containing:
1. `AppxManifest.xml`: The application configuration and capability declaration.
2. `Nemu.exe`: The compiled 64-bit executable.
3. Supporting runtime libraries (e.g. `libwinpthread-1.dll`, Direct3D 12 dependencies).
4. Visual assets: StoreLogo, Square150x150Logo, Square44x44Logo, SplashScreen.
5. `resources.pri`: Compiled package resource index (or simple resource directory).
6. `AppxBlockMap.xml` and `AppxSignature.p7x` (for signed distribution).

### Standard AppxManifest.xml Template for Nemu

```xml
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:mp="http://schemas.microsoft.com/appx/2014/phone/manifest"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap mp rescap">

  <Identity Name="Nemu.Xbox"
            Publisher="CN=NemuProject"
            Version="0.1.0.0"
            ProcessorArchitecture="x64" />

  <mp:PhoneIdentity PhoneProductId="a42f518e-491c-4b5c-b16f-ecb2f0a1c321"
                    PhonePublisherId="00000000-0000-0000-0000-000000000000" />

  <Properties>
    <DisplayName>Nemu</DisplayName>
    <PublisherDisplayName>Nemu Team</PublisherDisplayName>
    <Logo>Assets\StoreLogo.png</Logo>
  </Properties>

  <Dependencies>
    <TargetDeviceFamily Name="Windows.Universal"
                        MinVersion="10.0.15063.0"
                        MaxVersionTested="10.0.22621.0" />
    <PackageDependency Name="Microsoft.VCLibs.140.00"
                       MinVersion="14.0.30704.0"
                       Publisher="CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US" />
  </Dependencies>

  <Resources>
    <Resource Language="EN-US" />
  </Resources>

  <Applications>
    <Application Id="App"
                 Executable="Nemu.exe"
                 EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="Nemu"
                          Square150x150Logo="Assets\Square150x150Logo.png"
                          Square44x44Logo="Assets\Square44x44Logo.png"
                          Description="Nemu Nintendo Switch Emulator for Xbox Series S/X"
                          BackgroundColor="#107C10">
        <uap:SplashScreen Image="Assets\SplashScreen.png" BackgroundColor="#000000" />
      </uap:VisualElements>
    </Application>
  </Applications>

  <Capabilities>
    <Capability Name="internetClient" />
    <rescap:Capability Name="runFullTrust" />
    <rescap:Capability Name="broadFileSystemAccess" />
    <rescap:Capability Name="expandedResources" />
  </Capabilities>
</Package>
```

---

## 5. Deployment via Xbox Device Portal

1. Boot Xbox Series S/X into **Developer Mode**.
2. Note the console IP address and Device Portal port (e.g. `https://192.168.1.150:11443`).
3. Open a browser on the development PC and navigate to the Device Portal URL.
4. Authenticate using developer credentials.
5. In the **My games & apps** section, click **Add**.
6. Select `Nemu.appx` and dependency `Microsoft.VCLibs.x64.14.00.appx`.
7. Click **Deploy**.
8. Once installed, configure the application type to **Game** in the Xbox dashboard to unlock maximum GDDR6 memory (expanded resources) and full Zen 2 CPU core performance.
