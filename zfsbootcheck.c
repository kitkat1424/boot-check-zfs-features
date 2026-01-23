
#include <sys/sysctl.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* * 512KB is sufficient to cover gptzfsboot, which is usually < 200KB.
 * Set a limit because we are reading a raw device.
 */
#define SEARCH_LIMIT (512 * 1024) 

static int 
is_bios_boot() 
{
    char method[32];
    size_t len = sizeof(method);
    if (sysctlbyname("machdep.bootmethod", method, &len, NULL, 0) == 0) {
        if (strcmp(method, "BIOS") == 0) return 1;
    }
    return 0;
}

/*
 * Reads up to 'limit' bytes from path and scans for feature_string.
 * Returns 1 if found, 0 otherwise.
 */
static int 
disk_contains_string(const char *path, const char *feature_string, size_t limit) 
{
    int fd;
    char *buffer;
    ssize_t bytes_read;
    size_t str_len;
    int found = 0;
    off_t i;
    
    if ((fd = open(path, O_RDONLY)) < 0)
        return (0);

    if ((buffer = malloc(limit)) == NULL) {
        close(fd);
        return (0);
    }
    
    bytes_read = read(fd, buffer, limit);
    close(fd);

    if (bytes_read <= 0) {
        free(buffer);
        return (0);
    }

    str_len = strlen(feature_string);
    
    /* Ensure we don't read past the buffer if the string is at the very end */
    if ((size_t)bytes_read >= str_len) {
        for (i = 0; i <= bytes_read - (off_t)str_len; i++) {
            if (memcmp(buffer + i, feature_string, str_len) == 0) {
                found = 1;
                break;
            }
        }
    }

    free(buffer);
    return (found);
}

/* * Returns 1 if feature is active/enabled, 0 otherwise.
 */ 
static int 
is_pool_feature_enabled(const char *pool, const char *feature) 
{
    FILE *fp;
    char command[256];
    char output[256];
    
    snprintf(command, sizeof(command), 
            "zpool get -H -o value feature@%s %s 2>/dev/null", feature, pool);

    if ((fp = popen(command, "r")) == NULL)
        return 0; 

    if (fgets(output, sizeof(output), fp) != NULL) {
        output[strcspn(output, "\n")] = 0; 
        if (strcmp(output, "active") == 0 || 
            strcmp(output, "enabled") == 0) {
            pclose(fp);
            return 1;          
        }
    }
    pclose(fp);

    return (0); 
}

static void
usage(void)
{
    (void)fprintf(stderr, "usage: %s <pool_name> <partition_device>\n",
        getprogname());
    exit(1);
}

int 
main(int argc, char *argv[]) {
    char *disk_dev, *pool_name;
    int disk_has_zstd, pool_needs_zstd;

    if (argc < 3) 
        usage();
    
    pool_name = argv[1];
    disk_dev = argv[2];
    
    if (!is_bios_boot()) {
        printf("Boot method is not BIOS; skipping checks.\n");
        return (0);
    }

    /* Check requirements */
    pool_needs_zstd = is_pool_feature_enabled(pool_name, "zstd_compress");
    
    /* Check actual disk content */
    disk_has_zstd = disk_contains_string(disk_dev, "zstd_compress", SEARCH_LIMIT);
    
    if (pool_needs_zstd && !disk_has_zstd) 
        errx(3, "pool %s has zstd enabled, but bootloader on %s lacks support",
            pool_name, disk_dev);

    printf("Bootloader on %s is verified to support the required features.\n",
        disk_dev);

    return (0);
}
