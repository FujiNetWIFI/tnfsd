#ifndef _SESSION_H
#define _SESSION_H

/* The MIT License
 *
 * Copyright (c) 2010 Dylan Smith
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
 * TNFS session declarations
 *
 * */
#include <sys/types.h>

#include "tnfs.h"

/* Initialize TNFS */
void tnfs_init();

/* Returns 0 on success, -1 on error */
int tnfs_mount(Header *hdr, unsigned char *buf, int bufsz);
void tnfs_umount(Header *hdr, Session *s, int sindex);

/* Manage sessions */
/* Most functions also return the index of the session array
 * via the sindex pointer */

/* if withSid is nonzero, use the specified sid */
Session *tnfs_allocsession(int *sindex, uint16_t withSid);
void tnfs_freesession(Session *s, int sindex);
Session *tnfs_findsession_sid(uint16_t sid, int *sindex);
Session *tnfs_findsession_ipaddr(in_addr_t ipaddr, int *sindex);
void tnfs_reset_cli_fd_in_sessions(int cli_fd);
uint16_t tnfs_newsid();
uint16_t tnfs_session_count();
void tnfs_free_all_sessions();

#endif
