DLSS / FSR3 COMPATIBILITY PATCH - 0.6
https://github.com/brodrigz/xray-monolith

INSTALL
-------
1. Install the 0.6 DLSS / FSR3 binaries using that release's instructions.
2. Install and enable SSS 23.5 with GAMMA's recommended options.
3. Install this ZIP through MO2:
   - Page 1: leave the required SSS 23.5 option checked.
   - Page 2: choose your installed Atmospherics version, or None.
   - Page 3: select only the add-ons you have installed.
4. Enable this patch BELOW SSS, Atmospherics and any other shader mods
   in MO2's left pane.
5. Launch the DX11 game (or the experimental DX12 one) through MO2. In
   Settings > Video > Basic: set MSAA to Off, choose DLSS or FSR, then click
   Apply. Frame generation is in the same menu on the DX12 executable.

The original mods and binaries must be installed separately.
DLSS requires an NVIDIA RTX GPU. No special launch arguments are needed.

OPTIONAL ADD-ONS
---------------
WearableDevices:
  Select only if the original mod is installed and enabled.
  Disable the old separate WearableDevices DLSS patch.

Better Beef's NVGs (.19):
  Leave unchecked for stock GAMMA / stock Beef's NVG.
  Install original Beef's NVG, then Better Beef's with these modules:
    00 - Main
    02 - PATCH - [ SSS ]
    03 - PATCH - [ SSS Weapons DOF ]   if you use SSS Weapons DOF
    01 - PATCH - [ FDDA Redone ]       if you use FDDA Redone
  Put its patches below its main files, and this compatibility patch last.
  Clear the shader cache through the launcher after changing NVG mods.

UPDATING OR CHANGING OPTIONS
---------------------------
Reinstall this patch when changing Atmospherics or either optional add-on.
Choose REPLACE in MO2 so files from your old selections are removed.
Keep only one DLSS / FSR3 compatibility patch enabled.

CUSTOM SSS INSTALLATION ONLY
---------------------------
GAMMA users: use GAMMA's recommended SSS 23.5 options.
For a custom installation, include these SSS 23.5 modules:
  00 - MAIN FILE
  00 - MAIN FILE (MV)
  01 - AO
  02 - SSS
  03 - SSR
  04 - SSR Water                  (normal water, not tested with ice)
  05 - Rain - Puddles
  07 - Indirect Light
Keep any additional modules required by your modpack. Install SSS's
compatibility options only for mods you also have installed.

IF IT DOES NOT WORK
-------------------
Check your versions, installer choices and MO2 left-pane priority first.
If it still fails, share your full appdata/logs/xray_<username>.log,
enabled modlist and installer choices when asking for help.
