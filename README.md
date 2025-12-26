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
    * **Note:** I rely on hashing for this verification because the binary residing in the `freebsd-boot` partition is compressed or stripped, making it difficult to directly scan for feature strings or version numbers. But the binary in `/boot/gptzfsboot` can be scanned for strings. This check of identity ensures that the boot code and `/boot/gptzfsboot` are corresponding. 

3.  **Gate 3: Capability & Requirement Check**
    Scans the reference binary for specific feature signatures (`zstd_compress`) and compares them against the active features enabled on the ZFS pool.

## Scenarios Handled
* **Safe:** The boot partition matches the OS file, and the OS file supports all active ZFS pool features. **[PASS]**
* **Stale Bootloader:** The OS file is new, but the boot partition contains old/junk code (user forgot `gpart bootcode`). **[FAIL - IDENTITY]**
* **Failure:** The boot partition matches the OS file, but the OS file itself is too old to support the features currently enabled on the pool. **[FAIL - CAPABILITY]**

## Test Logic & Reproduction
[cite_start]The following commands can simulate the three scenarios using virtual disks and ZFS pools[cite: 1, 2].

### 1. Virtual setup
Created a dummy disk and ZFS pool that demands the `zstd` feature.
```bash
truncate -s 10M /tmp/test_disk.img
truncate -s 100M /tmp/zfs_disk.img
zpool create -f testpool /tmp/zfs_disk.img
zfs set compression=zstd testpool
```

2. A: "Stale Bootloader" (Identity Fail)
Simulate a user who updated the OS but left junk data in the boot partition.

```bash
# Corrupt the virtual boot partition
dd if=/dev/zero of=/tmp/test_disk.img bs=1m count=1

# Compile and Run
cc -o check-boot check-boot.c -lmd
./check-boot testpool /tmp/test_disk.img
# Expected: [FAIL] IDENTITY MISMATCH
```

3. B: "Safe"
Simulated a healthy system where the partition is synced with a capable bootloader.

```bash
# Sync real bootloader to virtual disk
dd if=/boot/gptzfsboot of=/tmp/test_disk.img conv=notrunc

./check-boot testpool /tmp/test_disk.img
# Expected: [PASS] Identity Confirmed... [PASS] CAPABILITY MATCH
```

4. C: "Failure" (Capability Fail)
Simulated an edge case where the bootloader is installed correctly but lacks necessary features (crippled binary).


```bash

# Create a "crippled" bootloader (remove zstd support string)
cp /boot/gptzfsboot /tmp/gptzfsboot.crippled
perl -pi -e 's/zstd_compress/xxxx_compress/g' /tmp/gptzfsboot.crippled

# Write crippled code to disk
dd if=/tmp/gptzfsboot.crippled of=/tmp/test_disk.img conv=notrunc

# Recompile tool (modify BOOT_FILE macro = '/tmp/gptzfsboot.crippled')
cc -o check-boot-test check-boot.c -lmd

# Run
./check-boot-test testpool /tmp/test_disk.img
# Expected: [PASS] Identity Confirmed... [CRITICAL] CAPABILITY FAILURE
```


