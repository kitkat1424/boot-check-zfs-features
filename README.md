# boot-check-zfs-features

## Problem 
Users can enable ZFS features (like `zstd`) on a root pool even if the installed legacy bootloader (`gptzfsboot`) is outdated and lacks support for them. So possibly, the system runs fine until the next reboot, at which point the bootloader fails to read the pool, making the system unbootable.
This is a proof-of-concept program that iterates through a hard-coded list of critical features. For each disk passed to the script, the first ~1mb is read and scanned for the feature strings.
It relies on the fact that a compiled bootloader binary that supports a feature will contain the feature name as a string.
