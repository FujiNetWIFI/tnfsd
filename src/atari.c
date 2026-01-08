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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include "atari.h"
#include "tnfs.h"
#include "config.h"

/* Module-private global state */
static bool atari_enabled = false;

/* Boot sector data (generated from atari_bootsector.bin) */
#ifdef ATARI_BOOTSECTOR_EXISTS
#include "atari_bootsector.h"
#else
/* Placeholder: empty boot sectors if no binary provided */
static const unsigned char atari_bootsector_bin[128] = {0};
static const unsigned int atari_bootsector_bin_len = 128;
#endif

/* Boot sector size - dynamically calculated, rounded up to multiple of 128 */
size_t atari_boot_sectors_size = 0;

/* Helper: Round up to nearest multiple of 128 */
static size_t round_up_128(size_t size)
{
	return ((size + 127) / 128) * 128;
}

/* Helper: Generate ATR header dynamically */
static void generate_atr_header(unsigned char *header)
{
	/* ATR header format (16 bytes, little-endian) */
	header[0] = 0x96;  /* Magic number low byte */
	header[1] = 0x02;  /* Magic number high byte */

	/* Paragraphs (size in 16-byte units, excluding header) */
	uint16_t count16 = ATR_DATA_SIZE / 16;  /* 130048 / 16 = 8128 = 0x1FC0 */
	header[2] = count16 & 0xFF;             /* 0xC0 */
	header[3] = (count16 >> 8) & 0xFF;      /* 0x1F */

	/* Sector size (128 bytes for single density) */
	header[4] = 0x80;  /* 128 low byte */
	header[5] = 0x00;  /* 128 high byte */

	/* High bytes of paragraph count (for >16MB images, not needed) */
	header[6] = 0x00;
	header[7] = 0x00;

	/* Reserved/unused bytes */
	memset(header + 8, 0, 8);
}

/* Helper: Extract file extension from path */
static const char *get_file_extension(const char *filepath)
{
	const char *dot = strrchr(filepath, '.');
	if (!dot || dot == filepath)
		return "";
	return dot + 1;
}

/* Helper: Check if extension is in exclusion list (case-insensitive) */
static bool is_excluded_extension(const char *ext)
{
	/* FujiNet handles these natively, don't virtualize */
	return (strcasecmp(ext, "xex") == 0 ||
	        strcasecmp(ext, "com") == 0 ||
	        strcasecmp(ext, "bin") == 0);
}

/* Helper: Minimum of two size_t values */
static inline size_t min_size(size_t a, size_t b)
{
	return (a < b) ? a : b;
}

/* Initialize Atari module */
void atari_init(bool enable_atari_mode)
{
	atari_enabled = enable_atari_mode;

	/* Calculate boot sector size, rounded up to multiple of 128 bytes */
	atari_boot_sectors_size = round_up_128(atari_bootsector_bin_len);

#ifdef DEBUG
	if (enable_atari_mode)
	{
		fprintf(stderr, "Atari mode initialized: boot sector file size=%u, rounded to %zu bytes\n",
				atari_bootsector_bin_len, atari_boot_sectors_size);
	}
#endif
}

/* Check if Atari mode is enabled */
bool atari_is_enabled(void)
{
	return atari_enabled;
}

/* Determine if a file should be virtualized as ATR
 * Returns true if:
 * - File is opened for reading only
 * - Extension is NOT .xex, .com, or .bin
 * - First two bytes are $FFFF
 */
bool atari_should_virtualize(const char *filepath, int flags)
{
	unsigned char magic[2];
	int fd;
	ssize_t n;
	const char *ext;

	if (!atari_enabled)
		return false;

	/* Only virtualize files opened for reading (not write/append/create) */
	if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_APPEND)) != 0)
		return false;

	/* Check if extension is in exclusion list */
	ext = get_file_extension(filepath);
	if (is_excluded_extension(ext))
		return false;

	/* Open file and check magic bytes */
	fd = open(filepath, O_RDONLY);
	if (fd < 0)
		return false;

	/* Read first 2 bytes */
	n = read(fd, magic, 2);
	close(fd);

	if (n != 2)
		return false;

	/* Check for $FFFF magic (Atari binary load file marker) */
	return (magic[0] == 0xFF && magic[1] == 0xFF);
}

/* Mark a file descriptor as Atari virtual */
void atari_mark_fd(Session *s, int tnfs_fd, off_t real_size)
{
	if (tnfs_fd < 0 || tnfs_fd >= MAX_FD_PER_CONN)
		return;

	s->atari_fd[tnfs_fd].is_atari_binary = true;
	s->atari_fd[tnfs_fd].virtual_position = 0;
	s->atari_fd[tnfs_fd].real_file_size = real_size;
}

/* Clear Atari metadata for a file descriptor */
void atari_clear_fd(Session *s, int tnfs_fd)
{
	if (tnfs_fd < 0 || tnfs_fd >= MAX_FD_PER_CONN)
		return;

	s->atari_fd[tnfs_fd].is_atari_binary = false;
	s->atari_fd[tnfs_fd].virtual_position = 0;
	s->atari_fd[tnfs_fd].real_file_size = 0;
}

/* Check if a file descriptor is an Atari virtual file */
bool atari_is_virtual_fd(Session *s, int tnfs_fd)
{
	if (tnfs_fd < 0 || tnfs_fd >= MAX_FD_PER_CONN)
		return false;

	return s->atari_fd[tnfs_fd].is_atari_binary;
}

/* Get virtual file size */
off_t atari_get_virtual_size(Session *s, int tnfs_fd)
{
	if (tnfs_fd < 0 || tnfs_fd >= MAX_FD_PER_CONN)
		return 0;

	if (!s->atari_fd[tnfs_fd].is_atari_binary)
		return 0;

	return ATR_TOTAL_SIZE;
}

/* Get virtual file position */
off_t atari_get_virtual_position(Session *s, int tnfs_fd)
{
	if (tnfs_fd < 0 || tnfs_fd >= MAX_FD_PER_CONN)
		return 0;

	return s->atari_fd[tnfs_fd].virtual_position;
}

/* Set virtual file position */
void atari_set_virtual_position(Session *s, int tnfs_fd, off_t pos)
{
	if (tnfs_fd < 0 || tnfs_fd >= MAX_FD_PER_CONN)
		return;

	s->atari_fd[tnfs_fd].virtual_position = pos;
}

/* Virtual read operation
 * Synthesizes data from four regions:
 * 1. ATR header (16 bytes)
 * 2. Boot sectors (compiled-in binary, 384 bytes)
 * 3. Real file data (from OS file descriptor)
 * 4. Zero padding (to reach 130KB data section)
 */
int atari_virtual_read(Session *s, int tnfs_fd, int os_fd,
                       unsigned char *buf, size_t count)
{
	atari_fd_metadata *meta = &s->atari_fd[tnfs_fd];
	off_t vpos = meta->virtual_position;
	off_t virtual_size = ATR_TOTAL_SIZE;
	size_t bytes_read = 0;
	unsigned char atr_header[ATR_HEADER_SIZE];
	off_t boot_end = ATR_HEADER_SIZE + atari_boot_sectors_size;
	off_t file_start = boot_end;
	off_t file_end = file_start + meta->real_file_size;

	/* Check for EOF */
	if (vpos >= virtual_size)
		return 0;

	/* Limit read to virtual EOF */
	if (vpos + (off_t)count > virtual_size)
		count = virtual_size - vpos;

	/* Generate ATR header once */
	generate_atr_header(atr_header);

	/* Read from appropriate region(s) */
	while (count > 0 && vpos < virtual_size) {
		if (vpos < ATR_HEADER_SIZE) {
			/* Region 1: ATR header (16 bytes) */
			size_t header_offset = vpos;
			size_t header_bytes = min_size(count, ATR_HEADER_SIZE - header_offset);
			memcpy(buf, atr_header + header_offset, header_bytes);
			buf += header_bytes;
			vpos += header_bytes;
			bytes_read += header_bytes;
			count -= header_bytes;
		}
		else if (vpos < boot_end) {
			/* Region 2: Boot sectors (from compiled binary, padded with zeros) */
			size_t boot_offset = vpos - ATR_HEADER_SIZE;
			size_t boot_bytes = min_size(count, boot_end - vpos);
			size_t total_boot_bytes = boot_bytes;

			/* Copy from actual boot sector data or zero padding */
			if (boot_offset < atari_bootsector_bin_len) {
				/* Still in actual boot sector data */
				size_t actual_bytes = min_size(boot_bytes, atari_bootsector_bin_len - boot_offset);
				memcpy(buf, atari_bootsector_bin + boot_offset, actual_bytes);
				buf += actual_bytes;
				boot_bytes -= actual_bytes;
			}

			/* Fill remaining with zeros (padding to round up to 128) */
			if (boot_bytes > 0) {
				memset(buf, 0, boot_bytes);
				buf += boot_bytes;
			}

			vpos += total_boot_bytes;
			bytes_read += total_boot_bytes;
			count -= total_boot_bytes;
		}
		else if (vpos < file_end) {
			/* Region 3: Actual file data */
			off_t file_offset = vpos - file_start;
			size_t file_bytes_available = file_end - vpos;
			size_t file_bytes = min_size(count, file_bytes_available);

			/* Seek to correct position in real file */
			if (lseek(os_fd, file_offset, SEEK_SET) < 0) {
				/* Seek error - return what we've read so far */
				break;
			}

			/* Read from real file */
			ssize_t n = read(os_fd, buf, file_bytes);
			if (n < 0) {
				/* Read error - return what we've read so far or error */
				if (bytes_read == 0)
					return -1;
				break;
			}

			buf += n;
			vpos += n;
			bytes_read += n;
			count -= n;

			/* Short read from file */
			if ((size_t)n < file_bytes)
				break;
		}
		else {
			/* Region 4: Zero padding to reach virtual size */
			size_t pad_bytes = min_size(count, virtual_size - vpos);
			memset(buf, 0, pad_bytes);
			buf += pad_bytes;
			vpos += pad_bytes;
			bytes_read += pad_bytes;
			count -= pad_bytes;
		}
	}

	/* Update virtual position */
	meta->virtual_position = vpos;
	return bytes_read;
}

/* Virtual seek operation
 * Updates virtual position without touching OS file descriptor
 */
off_t atari_virtual_lseek(Session *s, int tnfs_fd, int os_fd,
                          off_t offset, int whence)
{
	atari_fd_metadata *meta = &s->atari_fd[tnfs_fd];
	off_t virtual_size = ATR_TOTAL_SIZE;
	off_t new_pos;

	/* Calculate new virtual position based on whence */
	switch (whence) {
		case SEEK_SET:
			new_pos = offset;
			break;
		case SEEK_CUR:
			new_pos = meta->virtual_position + offset;
			break;
		case SEEK_END:
			new_pos = virtual_size + offset;
			break;
		default:
			errno = EINVAL;
			return -1;
	}

	/* Validate position (negative is error, but seeking past EOF is allowed) */
	if (new_pos < 0) {
		errno = EINVAL;
		return -1;
	}

	/* Update virtual position */
	meta->virtual_position = new_pos;
	return new_pos;
}
