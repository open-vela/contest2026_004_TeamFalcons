/* SPDX-License-Identifier: Apache-2.0 */
#ifndef VG_FILE_STORE_H
#define VG_FILE_STORE_H

#include <stddef.h>
#include <sys/types.h>

/* Longest file name component this store may create.  The board writes to a
 * vfat volume built with CONFIG_FAT_MAXFNAME=32, and a longer component fails
 * at open() with ENAMETOOLONG.  Measured on the board: 32 characters are
 * accepted, 33 are rejected.
 *
 * vg_file_replace derives three names from the caller's path, so the caller's
 * file name plus the longest suffix must stay inside this budget.  The backup
 * temporary was once "<name>.bak.tmp": for the product's
 * "point_table_candidate.json" that is 34 characters, so the backup step failed
 * and every candidate edit returned write_fail.  Keeping the suffix at five
 * characters holds that name at 31. */
#ifndef VG_STORE_NAME_MAX
#  define VG_STORE_NAME_MAX 32
#endif

/* Serialize writers, verify the complete temporary file before publishing,
 * and retain the previous bytes in path.bak. This is application-level
 * recovery, not a claim that FAT directory updates are power-fail atomic. */
int vg_file_replace(const char *path, const void *data, size_t len, mode_t mode);

/* Same transaction, but reject an existing destination under the writer lock. */
int vg_file_create(const char *path, const void *data, size_t len, mode_t mode);

#endif
