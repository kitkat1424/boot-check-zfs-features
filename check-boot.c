#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <md5.h>
#include <errno.h>

//#define BOOT_FILE "/tmp/gptzfsboot.crippled"

#define BOOT_FILE "/boot/gptzfsboot"
#define CHUNK_SIZE 4096

// Gate 1: Check Environment 
int 
is_bios_boot() {
    char method[32];
    size_t len = sizeof(method);
    if (sysctlbyname("machdep.bootmethod", method, &len, NULL, 0) == 0) {
        if (strcmp(method, "BIOS") == 0) return 1;
    }
    return 0;
}

// Gate 2: Hashing for identity check
int 
get_partial_hash(const char *path, off_t limit, unsigned char *hash_out) {
    MD5_CTX ctx;
    int fd;
    ssize_t bytes_read, total = 0;
    char buffer[CHUNK_SIZE];

    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    MD5Init(&ctx);
    while (total < limit) {
        size_t to_read = (limit - total > CHUNK_SIZE) ? CHUNK_SIZE : (limit - total);
        bytes_read = read(fd, buffer, to_read);
        if (bytes_read <= 0) break;
        MD5Update(&ctx, buffer, bytes_read);
        total += bytes_read;
    }

    close(fd);
    MD5Final(hash_out, &ctx);
    return (total == limit) ? 0 : -1;
}

// Gate 3a: Feature check of bootloader 
int 
file_supports_feature(const char *path, const char *feature_string) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    char *buffer = malloc(fsize);
    if (!buffer) { fclose(fp); return 0; }
    
    fread(buffer, 1, fsize, fp);
    fclose(fp);

    int found = 0;
    size_t len = strlen(feature_string);
    for (long i = 0; i < fsize - len; i++) {
        if (memcmp(buffer + i, feature_string, len) == 0) {
            found = 1;
            break;
        }
    }
    free(buffer);
    return found;
}

// Gate 3b : Feature requirements check of pool 
// Exit codes:-
//     1 = feature is enabled
//     0 = feature is disabled / pool does not existt
int 
is_pool_feature_enabled(const char *pool, const char *feature) {
    char command[256];
    char output[256];
    
    snprintf(command, sizeof(command), "zpool get -H -o value feature@%s %s 2>/dev/null", feature, pool);
    FILE *fp = popen(command, "r");
    if (!fp) return 0; 

    if (fgets(output, sizeof(output), fp) != NULL) {
        output[strcspn(output, "\n")] = 0; // Trim newline
        if (strcmp(output, "active") == 0 || strcmp(output, "enabled") == 0) {
            pclose(fp);
            return 1;         
        }
    }
    pclose(fp);
    return 0; 
}

int 
main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <pool_name> <partition_device>\n", argv[0]);
        printf("Example: %s testpool /tmp/disk.img\n", argv[0]);
        return 1;
    }

    char *pool_name = argv[1];
    char *disk_dev = argv[2];
    
    printf("=== ZFS Boot Diagnostic ===\n");
    printf("Target Pool: %s\nTarget Disk: %s\n\n", pool_name, disk_dev);

    // GATE 1: Environment
    if (!is_bios_boot()) {
        printf("[SKIP] Not Legacy BIOS. Skipping checks.\n");
        return 0;
    }

    // GATE 2: Identity check
    struct stat st;
    if (stat(BOOT_FILE, &st) != 0) {
        perror("Critical: OS bootloader missing");
        return 1;
    }
    off_t ref_size = st.st_size;

    unsigned char hash_ref[16], hash_disk[16];
    
    if (get_partial_hash(BOOT_FILE, ref_size, hash_ref) != 0 || 
        get_partial_hash(disk_dev, ref_size, hash_disk) != 0) {
        printf("[ERROR] I/O Error reading file or disk.\n");
        return 1;
    }

    if (memcmp(hash_ref, hash_disk, 16) != 0) {
        printf("[FAIL] IDENTITY MISMATCH\n");
        printf("   The bootloader on disk is NOT identical to %s.\n", BOOT_FILE);
        printf("   ACTION: Run 'gpart bootcode' immediately.\n");
        return 2; 
    }
    printf("[PASS] Identity Confirmed (Disk matches OS).\n");

    // GATE 3: Features (ZSTD Check)
    printf("[INFO] checking feature requirements...\n");
    
    int pool_needs_zstd = is_pool_feature_enabled(pool_name, "zstd_compress");
    int file_has_zstd   = file_supports_feature(BOOT_FILE, "zstd_compress");

    if (pool_needs_zstd) {
        if (file_has_zstd) {
            printf("[PASS] CAPABILITY MATCH\n");
            printf("   Pool has ZSTD enabled, and bootloader supports it.\n");
            printf("   System is SAFE to upgrade.\n");
            return 0;
        } else {
            printf("[CRITICAL] CAPABILITY FAILURE\n");
            printf("   Pool has ZSTD enabled, but bootloader LACKS support.\n");
            printf("   System may NOT BOOT.\n");
            return 3; // Exit Code 3 = Dangerous
        }
    } else {
        printf("[INFO] Pool does not use ZSTD. Bootloader capability irrelevant.\n");
        return 0;
    }
}
