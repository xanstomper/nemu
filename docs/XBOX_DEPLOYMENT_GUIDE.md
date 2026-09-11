# Xbox Series S/X Developer Mode Deployment Guide for Nemu

This guide explains how to install and execute **Nemu** on your Xbox Series S or Xbox Series X using Developer Mode.

---

## 1. Prerequisites

1. **Xbox Series S or Xbox Series X** console.
2. **Xbox Developer Mode Account** (registered via Microsoft Partner Center).
3. Host PC on the same Local Area Network (LAN) as your Xbox.
4. **Nemu Package:** `build-win/Nemu_1.0.0.0_x64.appx` (built via `scripts/package_xbox.sh`).

---

## 2. Preparing Xbox Developer Mode

1. On your Xbox console, switch to Developer Mode using the **Xbox Dev Mode** app.
2. Once in Developer Mode Home, note the **IP Address** and **Xbox Device Portal URL** displayed in the bottom-right corner:
   - Example: `https://192.168.1.150:11443`
3. Select **Dev Home Settings** -> **Xbox Device Portal**:
   - Ensure **Enable Xbox Device Portal** is checked.
   - Note the username and password (or set authentication if prompted).

---

## 3. Sideloading Nemu via Device Portal

1. On your PC, open a web browser (Chrome, Edge, Firefox) and navigate to:
   ```
   https://<XBOX_IP>:11443
   ```
   *(Accept the self-signed SSL security certificate warning).*
2. Log in using your Device Portal credentials.
3. In the left navigation menu, click **Apps**.
4. Under **Deploy App**:
   - Click **Choose File** for the application package and select:
     ```
     build-win/Nemu_1.0.0.0_x64.appx
     ```
   - Click **Next**.
   - If prompted for dependency packages, none are required (Nemu is statically linked).
   - Click **Start** to begin the installation.
5. Once deployment completes, **Nemu** will appear in the installed apps list.

---

## 4. Critical Configuration: Expanded Resources

To ensure Nemu can access full console hardware (~5 GB RAM on Series S, ~11 GB RAM on Series X, and all 8 CPU cores):

1. In the Xbox Dev Portal **Apps** list, locate **Nemu Switch Emulator**.
2. Click the gear / options icon next to Nemu.
3. Change **App Type** from `App` to **`Game`**.
   > [!IMPORTANT]
   > Changing App Type to `Game` is strictly required. Standard UWP apps are restricted to 2 CPU cores and 1–2 GB of RAM. The `Game` allocation grants access to the full expanded memory pool and maximum GPU compute capability.

---

## 5. Storage & Homebrew Management

Nemu supports loading homebrew either from internal console storage or external USB drives:

### External USB Setup (Recommended)
1. Format a USB 3.0 external flash drive or SSD as **NTFS**.
2. On the root of the drive, create a directory named:
   ```
   E:\nemu\sdmc\
   ```
3. Place Switch homebrew `.nro` files inside `E:\nemu\sdmc\`.
4. Plug the drive into any USB port on your Xbox Series S/X.
5. In Xbox Dev Home, ensure external storage permissions are enabled under **Settings** -> **Storage**.

---

## 6. Controller Bindings

Nemu directly reads Xbox Wireless Controllers via the low-latency native input stack:

| Xbox Controller Input | Nintendo Switch Mapping |
| :--- | :--- |
| **B Button** | **A Button** (Nintendo Confirm) |
| **A Button** | **B Button** (Nintendo Cancel) |
| **Y Button** | **X Button** |
| **X Button** | **Y Button** |
| **Left Bumper (LB)** | **L Button** |
| **Right Bumper (RB)** | **R Button** |
| **Left Trigger (LT)** | **ZL Trigger** |
| **Right Trigger (RT)** | **ZR Trigger** |
| **Menu / Start** | **Plus (+)** |
| **View / Back** | **Minus (-)** |
| **Left Thumbstick Click** | **L3 (StickL)** |
| **Right Thumbstick Click** | **R3 (StickR)** |
| **D-Pad** | **D-Pad Up / Down / Left / Right** |
| **Left / Right Sticks** | **Full 360° Analog with Radial Deadzone** |

---

## 7. Verification & Troubleshooting

- **Black screen on boot:** Verify that App Type is set to `Game` in Xbox Device Portal.
- **Missing homebrew files:** Check that USB drive is formatted as NTFS and mounted with appropriate permissions.
- **Log Files:** Nemu logs directly to the Xbox Local App Data folder and outputs to the debug console.
