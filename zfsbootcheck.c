/* zfsbootcheck - Verify ZFS bootloader capabilities against pool features
 *
 * Usage: zfsbootcheck <pool> <disk1> [disk2] ...
 * Exit Codes: 0 (OK), 1 (Warning), 2 (Critical)
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	SCAN_LIMIT_BYTES	(1024 * 1024)

struct feature_entry {
	const char *name;	/* Used for 'zpool get' AND searching in the binary */
	int needed;		/* Status flag: 1 if pool needs it */
};

static struct feature_entry critical_features[] = {
	{ "zstd_compress", 0 },
	{ "encryption",    0 },
	{ "large_blocks",  0 },
	{ "embedded_data", 0 },
	{ "lz4_compress",  0 },
	{ NULL, 0 }
};

static int
is_bios_boot(void)
{
	char method[32];
	size_t len;

	len = sizeof(method);
	if (sysctlbyname("machdep.bootmethod", method, &len, NULL, 0) == 0) {
		if (strcmp(method, "BIOS") == 0)
			return (1);
	}
	return (0);
}

static int
disk_scan_for_string(const char *path, const char *feature_string)
{
	char *buffer;
	ssize_t bytes_read;
	size_t len;
	long i;
	int fd, found;

	found = 0;

	if ((fd = open(path, O_RDONLY)) < 0) {
		warn("cannot open %s", path);
		return (0);
	}

	if ((buffer = malloc(SCAN_LIMIT_BYTES)) == NULL) {
		close(fd);
		err(1, "malloc failed");
	}

	bytes_read = read(fd, buffer, SCAN_LIMIT_BYTES);
	close(fd);

	if (bytes_read <= 0) {
		free(buffer);
		return (0);
	}

	len = strlen(feature_string);
	for (i= 0; i < bytes_read-(long)len; i++) {
		if (memcmp(buffer + i, feature_string, len) == 0) {
			found = 1;
			break;
		}
	}

	free(buffer);
	return (found);
}

static int
is_pool_feature_enabled(const char *pool, const char *feature)
{
	FILE *fp;
	char command[256];
	char output[256];
	int enabled;

	enabled = 0;
	snprintf(command, sizeof(command),
	    "zpool get -H -o value feature@%s %s 2>/dev/null", feature, pool);

	if ((fp = popen(command, "r")) == NULL)
		return (0);

	if (fgets(output, sizeof(output), fp) != NULL) {
		output[strcspn(output, "\n")] = 0;
		if (strcmp(output, "active") == 0 ||
		    strcmp(output, "enabled") == 0)
			enabled = 1;
	}
	pclose(fp);

	return (enabled);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s <pool> <disk1> [disk2] ...\n",
	    getprogname());
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct feature_entry *f;
	char *pool_name, *disk;
	int i, failed_disks;

	if (argc < 3)
		usage();

	pool_name = argv[1];

	if (!is_bios_boot())
		return (0);

	for (f = critical_features; f->name != NULL; f++) {
		f->needed = is_pool_feature_enabled(pool_name, f->name);
	}

	/* Scanning disks */
	failed_disks = 0;

	for (i = 2; i < argc; i++) {
		disk = argv[i];

		for (f = critical_features; f->name != NULL; f++) {
			if (f->needed && !disk_scan_for_string(disk, f->name)) {
				fprintf(stderr, "FAILED: %s (outdated boot code)\n", disk);
				failed_disks++;
				break; 
			}
		}
	}

	if (failed_disks == 0) {
		printf("OK: All %d boot disks verified.\n", argc - 2);
		return (0);
	}

	if (failed_disks == (argc - 2)) {
		fprintf(stderr, "CRITICAL: All %d disks failed check.\n",
		    failed_disks);
		return (2);
	}

	fprintf(stderr, "WARNING: %d/%d disks failed check.\n",
	    failed_disks, argc - 2);
	return (1);
}
