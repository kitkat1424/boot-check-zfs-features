# boot-check-zfs-features

A safety diagnostic tool for FreeBSD bootloaders (Legacy BIOS).
For now, the tool checks for `feature@zstd_compress` ZFS feature only. Other features that affect bootability will be added after confirming approach.

## Problem 
Users can enable ZFS features (like `zstd`) on a root pool even if the installed legacy bootloader (`gptzfsboot`) is outdated and lacks support for them. This can create a failure: the system runs fine until the next reboot, at which point the bootloader fails to read the pool, making the system unbootable.

## Methodology: Three Gates
This tool prevents it by enforcing a "Three Gate" safety check:

1.  **Gate 1: Environment Check**
    Verifies if the system is actually booting via Legacy BIOS. If the system is UEFI, the check is skipped for now.

2.  **Gate 2: Identity Check (Disk vs. File)**
    Ensures the boot code written to the disk partition is bit-for-bit identical to the reference system binary (`/boot/gptzfsboot`).
    * **Note:** Used hashing for this verification because the binary residing in the `freebsd-boot` partition is compressed, making it difficult to directly scan for feature strings or version numbers. But the binary in `/boot/gptzfsboot` can be scanned for strings. This check of identity ensures that the boot code and `/boot/gptzfsboot` are corresponding. 

3.  **Gate 3: Capability & Requirement Check**
    Scans the reference binary for specific feature signatures (`zstd_compress`) and compares them against the active features enabled on the ZFS pool.

## Scenarios Handled
* **Safe:** The boot partition matches the OS file (`/gpt/zfsboot`), and the OS file supports all active ZFS pool features. **[PASS]**
* **Outdated bootloader:** The OS file is new, but the boot partition contains old code (user forgot `gpart bootcode`). **[FAIL - IDENTITY]**
* **Failure:** The boot partition matches the OS file, but the OS file itself is too old to support the features currently enabled on the pool. **[FAIL - CAPABILITY]**

