#ifndef _ATARI_H
#define _ATARI_H

/* The MIT License
 *
 * Copyright (c) 2026
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Atari binary file virtualization as ATR disk images
 *
 * */

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* ATR disk image constants */
#define ATR_HEADER_SIZE 16
#define ATR_DATA_SIZE 256*1024  /* Virtual image size for binary load files */
#define ATR_TOTAL_SIZE (ATR_HEADER_SIZE + ATR_DATA_SIZE)

/* Boot sector size is determined dynamically at runtime from the actual file */
extern size_t atari_boot_sectors_size;

/* ATR file header format (16 bytes) */
struct atr_head {
	unsigned char h0;              /* 0x96 - Magic byte */
	unsigned char h1;              /* 0x02 - Magic byte */
	unsigned char bytes16count[2]; /* Size in paragraphs (16-byte units): 130048/16 = 8128 */
	unsigned char secsize[2];      /* Sector size: 128 (0x80, 0x00) */
	unsigned char hibytes16count[2]; /* High word of paragraph count (for >16MB images) */
	unsigned char unused[8];       /* Reserved, set to zero */
};

/* Forward declarations */
struct _session;
typedef struct _session Session;
/* atari_fd_metadata is fully defined in tnfs.h - no need to redeclare */

/* Module initialization */
void atari_init(bool enable_atari_mode);

/* Check if Atari mode is enabled */
bool atari_is_enabled(void);

/* File detection and setup */
bool atari_should_virtualize(const char *filepath, int flags);
void atari_mark_fd(Session *s, int tnfs_fd, off_t real_size);
void atari_clear_fd(Session *s, int tnfs_fd);

/* Virtual file operations */
bool atari_is_virtual_fd(Session *s, int tnfs_fd);
off_t atari_get_virtual_size(Session *s, int tnfs_fd);
off_t atari_get_virtual_position(Session *s, int tnfs_fd);
void atari_set_virtual_position(Session *s, int tnfs_fd, off_t pos);

/* Virtual read operation */
int atari_virtual_read(Session *s, int tnfs_fd, int os_fd,
                       unsigned char *buf, size_t count);

/* Virtual seek operation */
off_t atari_virtual_lseek(Session *s, int tnfs_fd, int os_fd,
                          off_t offset, int whence);

#endif
