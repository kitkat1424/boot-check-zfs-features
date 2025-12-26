# zfs-boot-check

A safety diagnostic tool for FreeBSD ZFS Bootloaders (Legacy BIOS).

## Problem Statement
FreeBSD allows users to enable modern ZFS features (like `zstd` compression) on a root pool even if the installed legacy bootloader (`gptzfsboot`) is outdated and lacks support for them. This creates a silent point of failure: the system runs fine until the next reboot, at which point the bootloader fails to read the pool, leaving the system unbootable.

## Methodology: The Three Gates
This tool prevents boot failures by enforcing a "Three Gate" safety check:

1.  **Gate 1: Environment Check**
    Verifies if the system is actually booting via Legacy BIOS. If the system is UEFI, the check is skipped to avoid false positives.

2.  **Gate 2: Identity Check (Disk vs. File)**
    Ensures the boot code written to the disk partition (`/dev/ada0p1`) is bit-for-bit identical to the reference system binary (`/boot/gptzfsboot`).
    * **Note:** We rely on hashing for this verification because the binary residing in the `freebsd-boot` partition is often compressed or stripped, making it difficult to directly scan for feature strings or version numbers.

3.  **Gate 3: Capability & Requirement Check**
    Scans the reference binary for specific feature signatures (e.g., `zstd_compress`) and compares them against the active features enabled on the ZFS pool.

## Scenarios Handled
* **The Safe System:** The boot partition matches the OS file, and the OS file supports all active ZFS pool features. **[PASS]**
* **The Stale Bootloader:** The OS file is new, but the boot partition contains old/junk code (user forgot `gpart bootcode`). **[FAIL - IDENTITY]**
* **The Silent Failure:** The boot partition matches the OS file, but the OS file itself is too old to support the features currently enabled on the pool. **[FAIL - CAPABILITY]**

## Test Logic & Reproduction
[cite_start]The following commands simulate the three scenarios using virtual disks and ZFS pools[cite: 1, 2].

### 1. Virtual setup
[cite_start]Created a dummy disk and ZFS pool that demands the `zstd` feature[cite: 3, 5, 6, 8].
```bash
truncate -s 10M /tmp/test_disk.img
truncate -s 100M /tmp/zfs_disk.img
zpool create -f testpool /tmp/zfs_disk.img
zfs set compression=zstd testpool
```

2. Scenario A: The "Stale Bootloader" (Identity Fail)
Simulate a user who updated the OS but left junk data in the boot partition.

```bash
# Corrupt the virtual boot partition
dd if=/dev/zero of=/tmp/test_disk.img bs=1m count=1

# Compile and Run
cc -o check-boot check-boot.c -lmd
./check-boot testpool /tmp/test_disk.img
# Expected: [FAIL] IDENTITY MISMATCH
```

3. Scenario B: The "Safe System" (Clean Pass)
Simulate a healthy system where the partition is synced with a capable bootloader.

```bash

# Sync real bootloader to virtual disk
dd if=/boot/gptzfsboot of=/tmp/test_disk.img conv=notrunc

# Run
./check-boot testpool /tmp/test_disk.img
# Expected: [PASS] Identity Confirmed... [PASS] CAPABILITY MATCH
```

4. Scenario C: The "Silent Failure" (Capability Fail)
Simulate an edge case where the bootloader is installed correctly but lacks necessary features (crippled binary).


```bash

# Create a "crippled" bootloader (remove zstd support string)
cp /boot/gptzfsboot /tmp/gptzfsboot.crippled
perl -pi -e 's/zstd_compress/xxxx_compress/g' /tmp/gptzfsboot.crippled

# Write crippled code to disk
dd if=/tmp/gptzfsboot.crippled of=/tmp/test_disk.img conv=notrunc

# Recompile tool to check against crippled file (modify BOOT_FILE macro first)
# sed -i '' 's|/boot/gptzfsboot|/tmp/gptzfsboot.crippled|' check-boot.c
cc -o check-boot-test check-boot.c -lmd

# Run
./check-boot-test testpool /tmp/test_disk.img
# Expected: [PASS] Identity Confirmed... [CRITICAL] CAPABILITY FAILURE
```

5. Cleanup
Remove the test environment.
```bash
zpool destroy testpool
rm /tmp/test_disk.img /tmp/zfs_disk.img /tmp/gptzfsboot.crippled
```
