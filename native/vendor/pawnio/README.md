# Bundled official PawnIO and Intel MSR module

IntelMCHBAR.bin is also embedded for read-only memory-controller traffic.
- Version: official PawnIO.Modules 0.2.11 (same release below).
- SHA-256: 3f82b832d99b4aac37d2a20fdb7c9baa2a3bc0488612c9019c9484eb0e8a6eae
- Matching source: IntelMCHBAR.p, https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMCHBAR.p
- Copyright 2026 Gen Li. LGPL-2.1-or-later; see COPYING.
- The application permits only dword reads at 0x5050 and 0x5054 on its supported
  Comet Lake layout. It uses an independent PawnIO handle and no write IOCTL.
- Counter interpretation cross-check: Intel PCM `ClientBW`,
  https://github.com/intel/pcm/blob/master/src/bw.cpp (64 bytes per transaction).

IntelMSR.bin is a 5,324-byte signed PawnIO module, not a DLL. It exposes restricted
register access. All temperature decoding, power calculation, validity checks,
sampling and device communication are implemented by ftop in C++.

- Upstream: https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11
- Release archive SHA-256: 43608cb89bc84247fef1368a139013f7d043e17db6d6c8dfc9b46bf0905a81f4
- IntelMSR.bin SHA-256: d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f
- Matching source: IntelMSR.p, https://github.com/namazso/PawnIO.Modules/blob/0.2.11/IntelMSR.p
- Copyright 2025 namazso. LGPL-2.1-or-later; see COPYING.
- Runtime: official signed PawnIO driver, installed automatically from the embedded official installer when absent.

The driver validates the module signature. Unsigned modified modules require
upstream signing for use with the official driver. ftop does not disable signature
checking, use the unrestricted driver, link PawnIOLib, or invoke any write function.

Interface references (no library code copied):
https://github.com/namazso/PawnIOLib/tree/f7a71a0cd611122c10bcdafe4ba5817dc520aff4

Algorithm cross-check:
https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/blob/76150732c2df9241415eec216001bec4557c9033/LibreHardwareMonitorLib/Hardware/Cpu/IntelCpu.cs

Official signed installer (unmodified): PawnIO 2.2.0
https://github.com/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe
SHA-256: 1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032
Installer publisher: namazso.eu. Installed kernel driver: Microsoft Windows Hardware Compatibility Publisher.
Driver source and license: https://github.com/namazso/PawnIO
Module source and COPYING are included alongside this file and in source.zip.
Both installer and module are embedded as EXE resources. No runtime download is needed.

The matching driver source archive PawnIO-2.2.0-source.zip is included in source.zip.
Tag 2.2.0 commit: 5cdf470831fdfff3f7f1d06363ca6b230f3bf35a
Archive SHA-256: c5e76738271e9d947f38af1b7ff1b584a37d559ed5d63b6d33108f0c6adcd7a1
Driver license text is also included as licenses/PawnIO-GPL.txt.
