/*
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@canonical.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>

#include <getopt.h>

#include <ccan/talloc/talloc.h>
#include <ccan/read_write_all/read_write_all.h>

#include "config.h"

#include "image.h"

static const char *toolname = "sbattach";

static struct option options[] = {
	{ "attach", required_argument, NULL, 'a' },
	{ "detach", required_argument, NULL, 'd' },
	{ "remove", no_argument, NULL, 'r' },
	{ "help", no_argument, NULL, 'h' },
	{ "version", no_argument, NULL, 'V' },
	{ NULL, 0, NULL, 0 },
};

static void usage(void)
{
	printf("Usage: %s --attach <sigfile> <efi-boot-image>\n"
		"   or: %s --detach <sigfile> [--remove] <efi-boot-image>\n"
		"   or: %s --remove <efi-boot-image>\n"
		"Attach or detach a signature file to/from a boot image\n"
		"\n"
		"Options:\n"
		"\t--attach <sigfile>  set <sigfile> as the boot image's\n"
		"\t                     signature table\n"
		"\t--detach <sigfile>  copy the boot image's signature table\n"
		"\t                     to <sigfile>\n"
		"\t--remove            remove the boot image's signature\n"
		"\t                     table from the original file\n",
		toolname, toolname, toolname);
}

static void version(void)
{
	printf("%s %s\n", toolname, VERSION);
}

static int detach_sig(struct image *image, const char *sig_filename)
{
	return image_write_detached(image, sig_filename);
}

static int attach_sig(struct image *image, const char *image_filename,
		const char *sig_filename)
{
	struct stat statbuf;
	uint8_t *sigbuf;
	size_t size;
	int fd, rc;

	sigbuf = NULL;

	fd = open(sig_filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Can't open file %s: %s\n", sig_filename,
				strerror(errno));
		return -1;
	}

	rc = fstat(fd, &statbuf);
	if (rc) {
		perror("fstat");
		goto out;
	}

	size = statbuf.st_size;

	sigbuf = talloc_array(image, uint8_t, size);
	if (!sigbuf) {
		perror("talloc");
		goto out;
	}

	rc = read_all(fd, sigbuf, size);
	if (!rc) {
		fprintf(stderr, "Error reading %s: %s\n", sig_filename,
				strerror(errno));
		goto out;
	}

	image->sigbuf = sigbuf;
	image->sigsize = size;

	rc = image_write(image, image_filename);
	if (rc)
		fprintf(stderr, "Error writing %s: %s\n", image_filename,
				strerror(errno));

out:
	close(fd);
	talloc_free(sigbuf);
	return rc;
}

static int remove_sig(struct image *image, const char *image_filename)
{
	int rc;

	image->sigbuf = NULL;
	image->sigsize = 0;

	rc = image_write(image, image_filename);
	if (rc)
		fprintf(stderr, "Error writing %s: %s\n", image_filename,
				strerror(errno));

	return rc;
}

enum action {
	ACTION_NONE,
	ACTION_ATTACH,
	ACTION_DETACH,
};

int main(int argc, char **argv)
{
	const char *image_filename, *sig_filename;
	struct image *image;
	enum action action;
	bool remove;
	int c, rc;

	action = ACTION_NONE;
	sig_filename = NULL;
	remove = false;

	for (;;) {
		int idx;
		c = getopt_long(argc, argv, "a:d:rhV", options, &idx);
		if (c == -1)
			break;

		switch (c) {
		case 'a':
		case 'd':
			if (action != ACTION_NONE) {
				fprintf(stderr, "Multiple actions specified\n");
				usage();
				return EXIT_FAILURE;
			}
			action = (c == 'a') ? ACTION_ATTACH : ACTION_DETACH;
			sig_filename = optarg;
			break;
		case 'r':
			remove = true;
			break;
		case 'V':
			version();
			return EXIT_SUCCESS;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		}
	}

	if (argc != optind + 1) {
		usage();
		return EXIT_FAILURE;
	}
	image_filename = argv[optind];

	/* sanity check action combinations */
	if (action == ACTION_ATTACH && remove) {
		fprintf(stderr, "Can't use --remove with --attach\n");
		return EXIT_FAILURE;
	}

	if (action == ACTION_NONE && !remove) {
		fprintf(stderr, "No action (attach/detach/remove) specified\n");
		usage();
		return EXIT_FAILURE;
	}

	image = image_load(image_filename);
	if (!image) {
		fprintf(stderr, "Can't load image file %s\n", image_filename);
		return EXIT_FAILURE;
	}

	rc = 0;

	if (action == ACTION_ATTACH)
		rc = attach_sig(image, image_filename, sig_filename);

	else if (action == ACTION_DETACH)
		rc = detach_sig(image, sig_filename);

	if (rc)
		goto out;

	if (remove)
		rc = remove_sig(image, image_filename);

out:
	talloc_free(image);
	return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}