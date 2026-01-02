#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOOT_FILE "/boot/gptzfsboot"
#define CHUNK_SIZE 4096

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

static int 
get_partial_hash(const char *path, off_t limit, unsigned char *hash_out) 
{
    MD5_CTX ctx;
    char buffer[CHUNK_SIZE];
    ssize_t bytes_read, total = 0;
    int fd;
    
    total = 0;
    if ((fd = open(path, O_RDONLY)) < 0) 
        return (-1);

    MD5Init(&ctx);
    while (total < limit) {
        size_t to_read; 
        to_read = (limit - total > CHUNK_SIZE) ? 
            CHUNK_SIZE : (limit - total);
        bytes_read = read(fd, buffer, to_read);
        if (bytes_read <= 0) 
            break;
        MD5Update(&ctx, buffer, bytes_read);
        total += bytes_read;
    }

    close(fd);
    MD5Final(hash_out, &ctx);

    return (total == limit) ? 0 : -1;
}

static int 
file_supports_feature(const char *path, const char *feature_string) 
{
    FILE *fp;
    char *buffer;
    long fsize, i;
    size_t len;
    int found = 0;
    
    if((fp = fopen(path, "rb")) == NULL)
        return (0);

    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    rewind(fp);

    if ((buffer = malloc(fsize)) == NULL) { 
        fclose(fp); 
        return 0; 
    }
    
    if(fread(buffer, 1, fsize, fp) != (size_t)fsize) {
        free(buffer);
        fclose(fp);
        return (0);
    }
    fclose(fp);

    found = 0;
    len = strlen(feature_string);
    for (i = 0; i < fsize - len; i++) {
        if (memcmp(buffer + i, feature_string, len) == 0) {
            found = 1;
            break;
        }
    }
    free(buffer);

    return (found);
}

/* 
 * Returns 1 if feature is active/enabled, 0 otherwise.
 */ 
static int 
is_pool_feature_enabled(const char *pool, const char *feature) 
{
    FILE *fp;
    char command[256];
    char output[256];
    
    snprintf(command, sizeof(command), 
            "zpool get -H -o value feature@%s %s 2>/dev/null", feature, pool);

    if((fp = popen(command, "r")) == NULL)
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
    struct stat st;
	off_t ref_size;
	unsigned char hash_disk[16], hash_ref[16];
	char *disk_dev, *pool_name;
	int file_has_zstd, pool_needs_zstd;

    if (argc < 3) 
        usage();
    

    pool_name = argv[1];
    disk_dev = argv[2];
    
    if (!is_bios_boot()) {
        printf("Boot method is not BIOS; skipping checks.\n");
        return (0);
    }

    if (stat(BOOT_FILE, &st) != 0) 
        err(1, "cannot stat %s", BOOT_FILE);

    ref_size = st.st_size;

    if (get_partial_hash(BOOT_FILE, ref_size, hash_ref) != 0)
		err(1, "failed to read %s", BOOT_FILE);
    if (get_partial_hash(disk_dev, ref_size, hash_disk) != 0) 
		err(1, "failed to read %s", disk_dev);

    if (memcmp(hash_ref, hash_disk, 16) != 0) 
   		errx(2, "checksum mismatch: %s on disk does not match %s",
		    disk_dev, BOOT_FILE);

    pool_needs_zstd = is_pool_feature_enabled(pool_name, "zstd_compress");
    file_has_zstd   = file_supports_feature(BOOT_FILE, "zstd_compress");
    
    if (pool_needs_zstd && !file_has_zstd) 
   		errx(3, "pool %s has zstd enabled, but bootloader lacks support",
		    pool_name);

   	printf("Bootloader on %s is verified and supports required features.\n",
	    disk_dev);

    return (0);
}
