# boot-check-zfs-features

## Problem 
Users can enable ZFS features (like `zstd`) on a root pool even if the installed legacy bootloader (`gptzfsboot`) is outdated and lacks support for them. This can create a failure: the system runs fine until the next reboot, at which point the bootloader fails to read the pool, making the system unbootable.

This is a proof-of-concept program that checks for the `feature@zstd_compress` ZFS feature only. Other features that affect bootability will be added.

