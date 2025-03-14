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
 * TNFS daemon datagram handler
 *
 * */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#include "tnfs.h"
#include "log.h"
#include "config.h"
#include "directory.h"
#include "tnfs_file.h"
#include "datagram.h"
#include "errortable.h"
#include "bsdcompat.h"
#include "endian.h"
#include "log.h"
#include "fileinfo.h"
#include "traverse.h"
#include "match.h"

#ifdef TNFS_DIR_EXT
#include <stdint.h>
#include <string.h>
struct tnfs_opendir_ext {
	struct dirent **namelist;
	int at, start, inc, total, visited;
	uint64_t seed;
	int do_reverse_list;        /* ;r reverse list order; default: forward list */
	int do_shuffle_list;        /* ;s shuffle list order; default: forward list. note: seed at [s+1] */
	int do_exclude_sysnames;    /* ;x hide system names like .git or .gitignore; default: show system files and dirs */
	int do_exclude_files;       // ;f hide file names; default: show files
	int do_exclude_dirs;        // ;d hide dir names; default: show dirs
	int do_uppercase;           // ;u "UPPER CASE" names; default: as-is
	int do_lowercase;           // ;l "lower case" names; default: as-is
	int do_camelcase;           // ;c "Camel Case" names; default: as-is
	char *wildcard;
};
static int alphacase_sort(const struct dirent **a, const struct dirent **b) {
	return strcasecmp((*a)->d_name, (*b)->d_name);
}
static int wildcard( const char *pattern, const char *str ) {
	if( *pattern=='\0' ) return !*str;
	if( *pattern=='*' )  return wildcard(pattern+1, str) || (*str && wildcard(pattern, str+1));
	if( *pattern=='?' )  return *str && (*str != '.') && wildcard(pattern+1, str+1);
	return ((*str|32) == (*pattern|32)) && wildcard(pattern+1, str+1);
}
static double rng(uint64_t *seed) { // returns [0,1)
	uint64_t z = (*seed += UINT64_C(0x9e3779b97f4a7c15));
	z = (z ^ (z >> 30)) *  UINT64_C(0xbf58476d1ce4e5b9);
	z = (z ^ (z >> 27)) *  UINT64_C(0x94d049bb133111eb);
	z = z ^ (z >> 31);
	return z / (double)0x100000000ULL;
}
static int is_prime(unsigned candidate) {
	/* GPL: https://github.com/kushaldas/elfutils/blob/master/lib/next_prime.c */
	/* No even number and none less than 10 will be passed here. */
	unsigned divn = 3;
	unsigned sq = divn * divn, old_sq;

	while (sq < candidate && candidate % divn != 0)
	{
		old_sq = sq;
		++divn;
		sq += 4 * divn;
		if (sq < old_sq)
			return 1;
		++divn;
	}

	return candidate % divn != 0;
}
static unsigned next_prime(unsigned seed) {
	/* GPL: https://github.com/kushaldas/elfutils/blob/master/lib/next_prime.c */
	seed |= 1; /* Make it odd */
	while (!is_prime(seed)) seed += 2;
	return seed;
}
#endif

directory_entry_list dirlist_concat(directory_entry_list list1, directory_entry_list list2);

void _tnfs_free_dir_handle(dir_handle* dhandle);

int _tnfs_find_free_dir_handle(Session *s, const char *path, uint8_t diropt, uint8_t sortopt, const char *pattern, bool reuse);

char root[MAX_ROOT]; /* root for all operations */
char realroot[MAX_ROOT]; /* full path of the tnfs root dir */
char dirbuf[MAX_FILEPATH];

int tnfs_setroot(const char *rootdir)
{
	if (strlen(rootdir) > MAX_ROOT)
		return -1;

#ifdef WIN32
	GetFullPathNameA(rootdir, MAX_ROOT, realroot, NULL);
#else
	realpath(rootdir, realroot);
#endif

	strlcpy(root, rootdir, MAX_ROOT);
	return 0;
}

/* validates a path points to an actual directory */
int validate_dir(Session *s, const char *path)
{
	char fullpath[MAX_TNFSPATH];
	struct stat dirstat;
	get_root(s, fullpath, MAX_TNFSPATH);

	/* relative paths are always illegal in tnfs messages */
	if (strstr(fullpath, "../") != NULL)
		return -1;

	normalize_path(fullpath, fullpath, MAX_TNFSPATH);
#ifdef DEBUG
	fprintf(stderr, "validate_dir: Path='%s'\n", fullpath);
#endif

	/* check we have an actual directory */
	if (stat(fullpath, &dirstat) == 0)
	{
		if (S_ISDIR(dirstat.st_mode))
		{
#ifdef DEBUG
			fprintf(stderr, "validate_dir: Directory OK\n");
#endif
			return 0;
		}
	}

	/* stat failed */
	return -1;
}

/* get the root directory for the given session */
void get_root(Session *s, char *buf, int bufsz)
{
	if (s->root == NULL)
	{
		snprintf(buf, bufsz, "%s/", root);
	}
	else
	{
		snprintf(buf, bufsz, "%s/%s/", root, s->root);
	}
}

/* validates the path is inside our root dir
   Returns 1 if path is inside tnfs root
   Returns 0 if path is outside tnfs root */
int validate_path(Session *s, const char *path)
{
	char valpath[MAX_FILEPATH];

#ifdef WIN32
	GetFullPathNameA(path, MAX_FILEPATH, valpath, NULL);
#else
	realpath(path, valpath);
#endif

#ifdef DEBUG
	fprintf(stderr, "validate path: %s::%s == ", valpath, realroot);
#endif

	if (strstr(valpath, realroot) != NULL)
	{
#ifdef DEBUG
	fprintf(stderr, "PASSED\n");
#endif
		return 1;
	}
	else
	{
#ifdef DEBUG
	fprintf(stderr, "FAILED\n");
#endif
		return 0;
	}
}

/* normalize paths, remove multiple delimiters
 * the new path at most will be exactly the same as the old
 * one, and if the path is modified it will be shorter so
 * doing "normalize_path(buf, buf, sizeof(buf)) is fine */
void normalize_path(char *newbuf, char *oldbuf, int bufsz)
{
	/* save newbuf; post-processed at end of function (TNFS_DIR_EXT only) */
	char *bakbuf = newbuf; (void)bakbuf;

	/* normalize the directory delimiters. Windows of course
	 * has problems with multiple delimiters... */
	int count = 0;
	int slash = 0;
#ifdef WIN32
	char *nbstart = newbuf;
#endif

	while (*oldbuf && count < bufsz - 1)
	{
		/* ...convert backslashes, too */
		if (*oldbuf != '/')
		{
			slash = 0;
			*newbuf++ = *oldbuf++;
		}
		else if (!slash && (*oldbuf == '/' || *oldbuf == '\\'))
		{
			*newbuf++ = '/';
			oldbuf++;
			slash = 1;
		}
		else if (slash)
		{
			oldbuf++;
		}
	}

	/* guarantee null termination */
	*newbuf = 0;

	/* remove a standalone trailing slash, it can cause problems
	 * with Windows, except for cases of "C:/" where it is
	 * mandatory */
#ifdef WIN32
	if (*(newbuf - 1) == '/' && strlen(nbstart) > 3)
		*(newbuf - 1) = 0;
#endif

#ifdef TNFS_DIR_EXT
	/* truncate wildcards and options from result. 'games/B?/snapshot.sna;xsi' => 'games/snapshot.sna' */
	if(strchr(bakbuf,'*') || strchr(bakbuf,'?') || strchr(bakbuf,';'))
	{
		char *output = strdup(bakbuf); output[0] = '\0';
		for(char *saveptr = 0, *foo = strtok_r(bakbuf,"/",&saveptr); foo; foo = strtok_r(NULL,"/",&saveptr))
		{
			if( !strchr(foo,'*') && !strchr(foo,'?') )
			{
				strcat(output, "/");
				strcat(output, foo);

				char *options = strchr(output, ';');
				if( options ) *options = '\0';

				if( options && options[-1]=='/' ) options[-1] = '\0';
			}
		}

		strcpy(bakbuf, output);
		free(output);

		char *options = strchr(bakbuf,';');
		if(options) *options = '\0';
	}
#endif
}

/* Open a directory */
void tnfs_opendir(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	DIR *dptr;
	char path[MAX_TNFSPATH];
	char normalized_path[MAX_TNFSPATH];
	unsigned char reply[2];
	int i;

	if (*(databuf + datasz - 1) != 0)
	{
#ifdef DEBUG
		fprintf(stderr, "Invalid dirname: no NULL\n");
#endif
		/* no null terminator */
		hdr->status = TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

#ifdef DEBUG
	fprintf(stderr, "opendir: %s\n", databuf);
#endif

	snprintf(path, MAX_TNFSPATH, "%s/%s/%s", root, s->root, databuf);
	normalize_path(normalized_path, path, MAX_TNFSPATH);

	/* set path to root if requested path is outside tnfs root */
	if (!validate_path(s, normalized_path))
		strcpy(normalized_path, root);

	/* find the first available slot in the session */
	i = _tnfs_find_free_dir_handle(s, normalized_path, 0, 0, NULL, false);
	if (i >= 0)
	{
#ifdef TNFS_DIR_EXT
		/* extract options from databuf if present at eos; truncates databuf */
		char *options = (char*)strrchr((const char *)databuf,';');
		if(options) *options++ = '\0';

		/* extract wildcard mask from path; extra work needed if /enclosed/ in a path; truncates databuf */
		char *mask = 0;
		if(strchr((const char *)databuf,'*') || strchr((const char*)databuf,'?')) {
			mask = strrchr((const char*)databuf,'/');
			if(mask) *mask = '\0', mask = strdup(mask+1);
			else mask = strdup((const char *)databuf), databuf[0] = 0;
		}

		/* build & normalize path */
		snprintf(path, MAX_TNFSPATH, "%s/%s/%s",
					root, s->root, databuf);
		normalize_path(s->dhandles[i].path, path, MAX_TNFSPATH);

		/* set path to root if requested path is outside tnfs root */
		if (!validate_path(s, s->dhandles[i].path))
			strcpy(s->dhandles[i].path, root);

		/* scan directory */
		struct dirent **namelist;
		int n = scandir(s->dhandles[i].path, &namelist, NULL, alphacase_sort);
		if(n>=0)
		{
			/* allocate iteration structure and options */
			struct tnfs_opendir_ext *handle = calloc(1, sizeof(struct tnfs_opendir_ext));
			handle->do_uppercase = options ? !!strchr(options,'u') : 0;
			handle->do_lowercase = options ? !!strchr(options,'l') : 0;
			handle->do_camelcase = options ? !!strchr(options,'c') : 0;
			handle->do_exclude_dirs = options ? !!strchr(options,'d') : 0;
			handle->do_exclude_files = options ? !!strchr(options,'f') : 0;
			handle->do_exclude_sysnames = options ? !!strchr(options,'x') : 0;
			handle->do_reverse_list = options ? !!strchr(options,'r') : 0;
			handle->do_shuffle_list = options ? !!strchr(options,'s') : 0;
			handle->seed = handle->do_shuffle_list ? strchr(options,'s')[1] : 123u;
			handle->at = handle->do_shuffle_list ? ((unsigned)rng(&handle->seed) * 100000) % (n-1) : (handle->do_reverse_list ? n - 1 : 0);
			handle->inc = handle->do_shuffle_list ? next_prime(n+(handle->seed&0xff)*7) : (handle->do_reverse_list ? -1 : 1);
			handle->visited = 0;
			handle->total = n;
			handle->namelist = namelist;
			handle->wildcard = mask;

			s->dhandles[i].handle = (void*)handle;
#else
		if ((dptr = opendir(s->dhandles[i].path)) != NULL)
		{
			s->dhandles[i].handle = dptr;
			s->dhandles[i].open = true;
#endif
			/* send OK response */
			hdr->status = TNFS_SUCCESS;
			reply[0] = (unsigned char)i;
			tnfs_send(s, hdr, reply, 1);
		}
		else
		{
			hdr->status = tnfs_error(errno);
			tnfs_send(s, hdr, NULL, 0);
		}

		/* done what is needed, return */
		return;
	}

	/* no free handles left */
	hdr->status = TNFS_EMFILE;
	tnfs_send(s, hdr, NULL, 0);
}

/* Read a directory entry */
void tnfs_readdir(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	struct dirent *entry;
	char reply[MAX_FILENAME_LEN];

	if (datasz != 1 ||
		*databuf > MAX_DHND_PER_CONN ||
		!s->dhandles[*databuf].open)
	{
		hdr->status = TNFS_EBADF;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

#ifdef TNFS_DIR_EXT
	/* visit entry */
	struct tnfs_opendir_ext *handle = (struct tnfs_opendir_ext*) s->dhandles[*databuf].handle; repeat:;
	if(handle->visited++ < handle->total)
	{
		/* handle forward, reverse and shuffle iterators */
		entry = handle->namelist[handle->at];
		handle->at = (handle->at + handle->inc) % handle->total;
		/* repeat if options and conditions do not match */
		if(handle->do_exclude_sysnames) if(entry->d_name[0] == '.') goto repeat;
		if(handle->wildcard) if(!wildcard(handle->wildcard, entry->d_name)) goto repeat;
		/* stat here for 'd' and 'f' flags. bypass if needed */
		if( handle->do_exclude_dirs ) if(entry->d_type == DT_DIR) goto repeat;
		if( handle->do_exclude_files ) if(entry->d_type != DT_DIR) goto repeat;
		/* convert to 'l'owercase/'u'ppercase/'c'amelcase here as needed */
                char *p = entry->d_name;
                /**/ if( handle->do_lowercase ) while(*p) *p++ = tolower(*p); //= *s | 32;
	        else if( handle->do_uppercase ) while(*p) *p++ = toupper(*p); //= *s & ~32;
	        else if( handle->do_camelcase ) while(*p) *p++ = (p == entry->d_name || p[-1] <= 32 ? toupper(*p) : tolower(*p));
#else
	entry = readdir(s->dhandles[*databuf].handle);
	if (entry)
	{
#endif
		strlcpy(reply, entry->d_name, MAX_FILENAME_LEN);
		hdr->status = TNFS_SUCCESS;
		tnfs_send(s, hdr, (unsigned char *)reply, strlen(reply) + 1);
	}
	else
	{
		hdr->status = TNFS_EOF;
		tnfs_send(s, hdr, NULL, 0);
	}
}

/* Close a directory */
void tnfs_closedir(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	if (datasz != 1 ||
		*databuf > MAX_DHND_PER_CONN ||
		!s->dhandles[*databuf].open)
	{
		hdr->status = TNFS_EBADF;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	s->dhandles[*databuf].open = false;

	hdr->status = TNFS_SUCCESS;
	tnfs_send(s, hdr, NULL, 0);
}

/* Make a directory */
void tnfs_mkdir(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	if (*(buf + bufsz - 1) != 0 ||
		tnfs_valid_filename(s, dirbuf, (char *)buf, bufsz) < 0)
	{
		hdr->status = TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
	}
	else
	{
#ifdef WIN32
		if (mkdir(dirbuf) == 0)
#else
		if (mkdir(dirbuf, 0755) == 0)
#endif
		{
			hdr->status = TNFS_SUCCESS;
			tnfs_send(s, hdr, NULL, 0);
		}
		else
		{
			hdr->status = tnfs_error(errno);
			tnfs_send(s, hdr, NULL, 0);
		}
	}
}

/* Remove a directory */
void tnfs_rmdir(Header *hdr, Session *s, unsigned char *buf, int bufsz)
{
	if (*(buf + bufsz - 1) != 0 ||
		tnfs_valid_filename(s, dirbuf, (char *)buf, bufsz) < 0)
	{
		hdr->status = TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
	}
	else
	{
		if (rmdir(dirbuf) == 0)
		{
			hdr->status = TNFS_SUCCESS;
			tnfs_send(s, hdr, NULL, 0);
		}
		else
		{
			hdr->status = tnfs_error(errno);
			tnfs_send(s, hdr, NULL, 0);
		}
	}
}

void tnfs_seekdir(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	uint32_t pos;

	// databuf holds our directory handle
	// followed by 4 bytes for the new position
	if (datasz != 5 ||
		*databuf > MAX_DHND_PER_CONN ||
		!s->dhandles[*databuf].open ||
		(s->dhandles[*databuf].entry_list == NULL && s->dhandles[*databuf].handle == NULL))
	{
		hdr->status = TNFS_EBADF;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	pos = tnfs32uint(databuf + 1);
#ifdef DEBUG
	fprintf(stderr, "tnfs_seekdir to pos %u\n", pos);
#endif

	// We handle this differently depending on whether we've pre-loaded the directory or not
	if (s->dhandles[*databuf].entry_list == NULL)
	{
		seekdir(s->dhandles[*databuf].handle, (long)pos);
	}
	else
	{
		s->dhandles[*databuf].current_entry = dirlist_get_node_at_index(s->dhandles[*databuf].entry_list, pos);
	}
#ifdef USAGELOG
	if (pos == 0) {
		if (strcmp(s->lastpath, s->dhandles[*databuf].path) != 0) {
				USGLOG(hdr, "Path changed to: %s", s->dhandles[*databuf].path);
		};
		strcpy(s->lastpath, s->dhandles[*databuf].path);
	}
#endif


	hdr->status = TNFS_SUCCESS;
	tnfs_send(s, hdr, NULL, 0);
}

void tnfs_telldir(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	int32_t pos;

	// databuf holds our directory handle: check it
	if (datasz != 1 ||
		*databuf > MAX_DHND_PER_CONN ||
		!s->dhandles[*databuf].open ||
		(s->dhandles[*databuf].entry_list == NULL && s->dhandles[*databuf].handle == NULL))
	{
		hdr->status = TNFS_EBADF;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	// We handle this differently depending on whether we've pre-loaded the directory or not
	if (s->dhandles[*databuf].entry_list == NULL)
	{
		pos = telldir(s->dhandles[*databuf].handle);
	}
	else
	{
		pos = dirlist_get_index_for_node(s->dhandles[*databuf].entry_list, s->dhandles[*databuf].current_entry);
	}

#ifdef DEBUG
	fprintf(stderr, "tnfs_telldir returning %d\n", pos);
#endif

	hdr->status = TNFS_SUCCESS;
	uint32tnfs((unsigned char *)&pos, (uint32_t)pos);

	tnfs_send(s, hdr, (unsigned char *)&pos, sizeof(pos));
}

/* Read a directory entry and provide extended results */
void tnfs_readdirx(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	/*
	The response starts with:
	count - 1 byte: number of entries returned
	status- 1 byte: directory status
	dpos  - 2 bytes: directory position of first returned entry (as TELLDIR would return)

	With each entry we're returning:
	flags - 1 byte: Flags providing additional information about the file
	size  - 4 bytes: Unsigned 32 bit little endian size of file in bytes
	mtime - 4 bytes: Modification time in seconds since the epoch, little endian
	ctime - 4 bytes: Creation time in seconds since the epoch, little endian
	entry - X bytes: Zero-terminated string providing directory entry path
*/
	uint8_t sid;
	// databuf holds our directory handle followed by number of entries requested
	if (datasz != 2 ||
		(sid = databuf[0]) > MAX_DHND_PER_CONN)
	{
		hdr->status = TNFS_EBADF;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}
	// req_count of '0' means "as many as will fit in the reply"
	// any other value sets a max number of replies to send
	uint8_t req_count = databuf[1];

	dir_handle *dh = &s->dhandles[sid];
#ifdef DEBUG
/*  // Force a delay to check handling on the client
	LOG("A LITTLE PAUSE\n");
	unsigned long wl = 0;
	for(int w=0; w < 1000000000LL; w++)
		wl = wl + w * w;
	LOG("PAUSE = %lu\n", wl);
*/
#endif

	// Return EOF if we're already at the end of the list
	if (dh->current_entry == NULL)
	{
#ifdef DEBUG
		TNFSMSGLOG(hdr, "readdirx no more entries - returning EOF");
#endif
		hdr->status = TNFS_EOF;
		tnfs_send(s, hdr, NULL, 0);
	}

#ifdef DEBUG
	TNFSMSGLOG(hdr, "readdirx request for %hu entries", req_count);
#endif

/* The number of bytes required by the response 'header'
 response_count (1) + dir_status (1) + dirpos (2) = 4 bytes
*/
#define READDIRX_HEADER_SIZE 4

/* The number of bytes each entry takes not including the
 length of the actual file/directory name
 flags (1) + size (4) + mtime (4) + ctime(4) + NULL (1) = 14 bytes
 */
#define READDIRX_ENTRY_SIZE 14

	// our reply must hold up to TNFS_MAX_PAYLOAD bytes
	uint8_t reply[TNFS_MAX_PAYLOAD];
	// set the reply count to 0
	reply[0] = 0;
	// set the status to 0
	reply[1] = 0;

	directory_entry *pThisEntry, *pEntryInReply;
	// Start by pointing to just after the reply 'header' in the buffer
	pEntryInReply = (directory_entry *)(reply + READDIRX_HEADER_SIZE);

	uint8_t count_sent = 0;
	int total_size = READDIRX_HEADER_SIZE;

	while (dh->current_entry != NULL)
	{
		// Quit if we've reached the requested count
		if (req_count != 0 && count_sent >= req_count)
			break;

		pThisEntry = &dh->current_entry->entry;
		int namelen = strlen(pThisEntry->entrypath);

		// Quit if this entry won't fit in what's left of the reply buffer
		if ((total_size + READDIRX_ENTRY_SIZE + namelen) > sizeof(reply))
			break;

		// If this is the first entry, copy the directory position into the reply
		if (count_sent == 0)
			uint16tnfs(reply + 2, dirlist_get_index_for_node(dh->entry_list, dh->current_entry));

		// Copy the entry data into the appropriate spots in the reply buffer
		strcpy(pEntryInReply->entrypath, pThisEntry->entrypath);

		pEntryInReply->flags = pThisEntry->flags;
		uint32tnfs((unsigned char *)&pEntryInReply->size, pThisEntry->size);
		uint32tnfs((unsigned char *)&pEntryInReply->mtime, pThisEntry->mtime);
		uint32tnfs((unsigned char *)&pEntryInReply->ctime, pThisEntry->ctime);

		// Update our count and save it in the reply
		count_sent++;
		reply[0] = count_sent;

		// Keep track of how much of the buffer we've used
		total_size += READDIRX_ENTRY_SIZE + namelen;
		// Move our pointer within the reply to the end of the current entry
		pEntryInReply = (directory_entry *)(reply + total_size);

		// Point to the next directory entry
		dh->current_entry = dh->current_entry->next;
	}

	// If we've reached the end of the directory, set the TNFS_DIRSTATUS_EOF flag
	if(dh->current_entry == NULL)
		reply[1] |= TNFS_DIRSTATUS_EOF;

	// Respond with whatever we've collected
	hdr->status = TNFS_SUCCESS;
#ifdef DEBUG
	TNFSMSGLOG(hdr, "readdirx responding with %hu entries, status_flags=0x%x", reply[0], reply[1]);
#endif
	tnfs_send(s, hdr, reply, total_size);
}

// Returns false if pattern doesn't match, otherwise true
bool _pattern_match(const char *src, const char *pattern)
{
	if (src == NULL || pattern == NULL)
		return false;

	int m = strlen(pattern);
	int n = strlen(src);

#ifdef DEBUG
	fprintf(stderr, "Pattern match: \"%s\", \"%s\" = ", src, pattern);
#endif
	// Empty pattern can only match with empty string
	if (m == 0)
		return (n == 0);

	// Lookup table for storing results of subproblems
	bool lookup[n + 1][m + 1];

	// Initailze lookup table to false
	memset(lookup, false, sizeof(lookup));

	// Empty pattern can match with empty string
	lookup[0][0] = true;

	// Only '*' can match with empty string
	for (int j = 1; j <= m; j++)
		if (pattern[j - 1] == '*')
			lookup[0][j] = lookup[0][j - 1];

	// Fill the table in bottom-up fashion
	for (int i = 1; i <= n; i++)
	{
		for (int j = 1; j <= m; j++)
		{
			// Two cases if we see a '*':
			// a) We ignore '*' character and move to next character in the pattern,
			//     i.e., '*' indicates an empty sequence.
			// b) '*' character matches with i-th character in input
			if (pattern[j - 1] == '*')
			{
				lookup[i][j] = lookup[i][j - 1] || lookup[i - 1][j];

			// Current characters are considered matching in two cases:
			// (a) current character of pattern is '?'
			// (b) characters actually match (case insensitive)
			} else if (pattern[j - 1] == '?' ||
				 (src[i - 1] == pattern[j - 1]) ||
				 (tolower(src[i - 1]) == tolower(pattern[j - 1])))
			{
				lookup[i][j] = lookup[i - 1][j - 1];
			}
			// If characters don't match
			else
				lookup[i][j] = false;
		}
	}
	bool result = lookup[n][m];

#ifdef DEBUG
	fprintf(stderr, "%s\n", result ? "TRUE" : "FALSE");
#endif
	return result;
}

/* Returns errno on failure, otherwise zero */
int _load_directory(dir_handle *dirh, uint8_t diropts, uint8_t sortopts, uint16_t maxresults, const char *pattern)
{
	struct dirent *entry;
	char statpath[MAX_TNFSPATH];
	char temp_statpath[MAX_TNFSPATH*2 + 4];

	// Free any existing entries
	dirlist_free(dirh->entry_list);
	dirh->entry_count = 0;

	if ((dirh->handle = opendir(dirh->path)) == NULL)
		return errno;

	// A list to hold all subdirectory names
	directory_entry_list list_dirs = NULL;
	// A list to hold all normal file names
	directory_entry_list list_files = NULL;
	uint16_t entrycount = 0;

	// Read every entry
	while ((entry = readdir(dirh->handle)) != NULL)
	{
		// Try to stat the file before we can decide on other things
		fileinfo_t finf;
		snprintf(temp_statpath, sizeof(temp_statpath), "%s%c%s", dirh->path, FILEINFO_PATHSEPARATOR, entry->d_name);
		strncpy(statpath, temp_statpath, sizeof(statpath));
		if (get_fileinfo(statpath, &finf) == 0)
		{
			/* If it's not a directory and we have a pattern that this doesn't match, skip it
				Ignore the directory qualification if TNFS_DIROPT_DIR_PATTERN is set */
			if ((diropts & TNFS_DIROPT_DIR_PATTERN) || !(finf.flags & FILEINFOFLAG_DIRECTORY))
			{
				if (pattern != NULL && _pattern_match(entry->d_name, pattern) == false)
					continue;
			}

			// Skip this if it's hidden (assuming TNFS_DIROPT_NO_SKIPHIDDEN isn't set)
			if (!(diropts & TNFS_DIROPT_NO_SKIPHIDDEN) && (finf.flags & FILEINFOFLAG_HIDDEN))
				continue;

			// Skip this if it's special (assuming TNFS_DIROPT_NO_SKIPSPECIAL isn't set)
			if (!(diropts & TNFS_DIROPT_NO_SKIPSPECIAL) && (finf.flags & FILEINFOFLAG_SPECIAL))
				continue;

			if ((diropts & TNFS_DIROPT_NO_FOLDERS) && (finf.flags & FILEINFOFLAG_DIRECTORY))
				continue;

			// Create a new directory_entry_node to add to our list
			directory_entry_list_node *node = calloc(1, sizeof(directory_entry_list_node));

			// Copy the name into the node
			strlcpy(node->entry.entrypath, entry->d_name, MAX_FILENAME_LEN);

			directory_entry_list *list_dest_p = &list_files;

			if (finf.flags & FILEINFOFLAG_DIRECTORY)
			{
				node->entry.flags = finf.flags;
				/* If the TNFS_DIROPT_NO_FOLDERSFIRST 0x01  flag hasn't been set, put this node
				   in a separate list for directories so they're sorted separately */
				if (!(diropts & TNFS_DIROPT_NO_FOLDERSFIRST))
					list_dest_p = &list_dirs;
			}
			node->entry.size = finf.size;
			node->entry.mtime = finf.m_time;
			node->entry.ctime = finf.c_time;

			dirlist_push(list_dest_p, node);
			entrycount++;

			// If we were given a max, break if we've reached it
			if (maxresults > 0 && entrycount >= maxresults)
				break;
#ifdef DEBUG
			//fprintf(stderr, "_load_directory added \"%s\" %u\n", node->entry.entrypath, node->entry.size);
#endif
		}
	}

	// Sort the two lists (assuming TNFS_DIRSORT_NONE isn't set)
	if (!(sortopts & TNFS_DIRSORT_NONE))
	{
		if (list_dirs != NULL)
			dirlist_sort(&list_dirs, sortopts);
		if (list_files != NULL)
			dirlist_sort(&list_files, sortopts);
	}

	// Combine the two lists into one
	dirh->entry_list = dirlist_concat(list_dirs, list_files);
	dirh->entry_count = entrycount;

#ifdef DEBUG
/*
	fprintf(stderr, "RETURNING LIST:\n");
	directory_entry_list _dl = dirh->entry_list;
	while (_dl)
	{
		fprintf(stderr, "\t%s\n", _dl->entry.entrypath);
		_dl = _dl->next;
	}
*/
#endif

	dirh->current_entry = dirh->entry_list;

	return 0;
}

/* Open a directory with additional options */
void tnfs_opendirx(Header *hdr, Session *s, unsigned char *databuf, int datasz)
{
	char path[MAX_TNFSPATH];
	char normalized_path[MAX_TNFSPATH];
	unsigned char reply[3];

	uint8_t diropts;
	uint8_t sortopts;
	uint16_t maxresults;
	uint8_t result;
	char *pPattern;
	char *pDirpath;

	int i;

	// We should have a minimum of 7 bytes in the request
	// And the buffer should be null-terminated
	if ((datasz < 7) || (*(databuf + datasz - 1) != 0))
	{
#ifdef DEBUG
		TNFSMSGLOG(hdr, "Invalid argument count or missing NULL terminator");
#endif
		hdr->status = TNFS_EINVAL;
		tnfs_send(s, hdr, NULL, 0);
		return;
	}

	diropts = databuf[0];
	sortopts = databuf[1];
	maxresults = tnfs16uint(databuf + 2);
	pPattern = (char *)(databuf + 4);

	// If there's no NULL between the glob pattern and the directory name,
	// just assume there's no glob pattern rather than return an error
	i = strlen(pPattern);
	if (i + 5 == datasz)
	{
		pDirpath = pPattern;
		pPattern = NULL;
	}
	else
	{
		pDirpath = pPattern + i + 1;
	}
	if (i == 0)
		pPattern = NULL;

#ifdef DEBUG
	TNFSMSGLOG(hdr, "opendirx: diropt=0x%02x, sortopt=0x%02x, max=0x%04hx, pat=\"%s\", path=\"%s\"",
			diropts, sortopts, maxresults, pPattern ? pPattern : "", pDirpath);
#endif

	snprintf(path, sizeof(path), "%s/%s/%s", root, s->root, pDirpath);

	// Remove any doubled-up path separators
	normalize_path(normalized_path, path, MAX_TNFSPATH);

	/* set path to root if requested path is outside tnfs root */
	if (!validate_path(s, normalized_path))
		strcpy(normalized_path, root);

	/* find the first available slot in the session */
	i = _tnfs_find_free_dir_handle(s, normalized_path, diropts, sortopts, pPattern, (diropts & TNFS_DIROPT_TRAVERSE) != 0);
	if (i >= 0)
	{
		if (diropts & TNFS_DIROPT_TRAVERSE)
		{
			if (s->dhandles[i].loaded)
			{
				// reuse already loaded dir
				result = 0;
			}
			else
			{
				result = _traverse_directory(&(s->dhandles[i]), diropts, sortopts, maxresults, pPattern);
			}
		}
		else
		{
			result = _load_directory(&(s->dhandles[i]), diropts, sortopts, maxresults, pPattern);
		}
		if (result == 0)
		{
			s->dhandles[i].open = true;
			s->dhandles[i].loaded = true;

			/* send OK response */
			hdr->status = TNFS_SUCCESS;
			#ifdef DEBUG
			TNFSMSGLOG(hdr, "opendirx response: handle=%hu, count=%hu", i, s->dhandles[i].entry_count);
			#endif
			reply[0] = (unsigned char) i;
			uint16tnfs(reply + 1, s->dhandles[i].entry_count);
			tnfs_send(s, hdr, reply, 3);
		}
		else
		{
			hdr->status = tnfs_error(result);
			tnfs_send(s, hdr, NULL, 0);
		}

		/* done what is needed, return */
		return;
	}

	/* no free handles left */
	hdr->status = TNFS_EMFILE;
	tnfs_send(s, hdr, NULL, 0);
}

// Attaches list2 to the end of list1 and returns the head of the result
// Return will be list2 if list1 is NULL
directory_entry_list dirlist_concat(directory_entry_list list1, directory_entry_list list2)
{
	if (list1 == NULL)
		return list2;

	// Get to the end of the first list
	directory_entry_list_node *pEnd = list1;
	while (pEnd->next != NULL)
		pEnd = pEnd->next;

	pEnd->next = list2;
	return list1;
}

void dirlist_push(directory_entry_list *dlist, directory_entry_list_node *node)
{
	if (dlist == NULL || node == NULL)
		return;

	// This entry becomes the head, any current head becomes the second node
	node->next = *dlist;
	*dlist = node;
}

/* Returns poitner to node at index given or NULL if no such index */
directory_entry_list_node *dirlist_get_node_at_index(directory_entry_list dlist, uint32_t index)
{
	uint32_t i = 0;
	while (dlist && i++ < index)
		dlist = dlist->next;

	return dlist;
}

/* Returns poitner to node at index given or NULL if no such index */
uint32_t dirlist_get_index_for_node(directory_entry_list dlist, directory_entry_list_node *node)
{
	uint32_t i = 0;
	while (dlist)
	{
		if (dlist == node)
			break;
		dlist = dlist->next;
		i++;
	}

	return i;
}

/* Free the linked list of directory entries */
void dirlist_free(directory_entry_list dlist)
{
	while (dlist)
	{
		directory_entry_list_node *next = dlist->next;
		free(dlist);
		dlist = next;
	}
}

directory_entry_list _mergesort_merge(directory_entry_list list_left, directory_entry_list list_right, uint8_t sortopts)
{
	if (list_left == NULL)
		return list_right;
	if (list_right == NULL)
		return list_left;

	directory_entry_list result;

	int r;
	// Sort by size
	if (sortopts & TNFS_DIRSORT_SIZE)
	{
		r = list_left->entry.size - list_right->entry.size;
	}
	// Sort by modified timestamp
	else if (sortopts & TNFS_DIRSORT_MODIFIED)
	{
		r = list_left->entry.mtime - list_right->entry.mtime;
	}
	// Sort by name
	else
	{
		// Decide whether to use case-sensitive or insensitive sorting
		if (sortopts & TNFS_DIRSORT_CASE)
			r = strcmp(list_left->entry.entrypath, list_right->entry.entrypath);
		else
			r = strcasecmp(list_left->entry.entrypath, list_right->entry.entrypath);
	}

	// Reverse the result if we're sorting descending
	if (sortopts & TNFS_DIRSORT_DESCENDING)
		r *= -1;

	if (r < 0)
	{
		result = list_left;
		result->next = _mergesort_merge(list_left->next, list_right, sortopts);
	}
	else
	{
		result = list_right;
		result->next = _mergesort_merge(list_left, list_right->next, sortopts);
	}

	return result;
}

directory_entry_list _mergesort_get_middle(directory_entry_list head)
{
	if (head == NULL)
		return head;

	directory_entry_list slow, fast;
	slow = fast = head;

	while (fast->next != NULL && fast->next->next != NULL)
	{
		slow = slow->next;
		fast = fast->next->next;
	}
	return slow;
}

void _mergesort(directory_entry_list *headP, uint8_t sortopts)
{
	directory_entry_list head = *headP;

	if (head == NULL || head->next == NULL)
		return;

	directory_entry_list list_left;
	directory_entry_list list_right;
	directory_entry_list list_mid;

	// Split the list into two separate lists
	list_left = head;
	list_mid = _mergesort_get_middle(head);
	list_right = list_mid->next;
	list_mid->next = NULL;

	// Merge the two lists
	_mergesort(&list_left, sortopts);
	_mergesort(&list_right, sortopts);
	*headP = _mergesort_merge(list_left, list_right, sortopts);
}

/* Merge sort on a singly-linked list */
void dirlist_sort(directory_entry_list *dlist, uint8_t sortopts)
{
	_mergesort(dlist, sortopts);
}

void _tnfs_free_dir_handle(dir_handle* dhandle)
{
	#ifdef TNFS_DIR_EXT
	/* deallocate ext iterator */
	struct tnfs_opendir_ext *handle = (struct tnfs_opendir_ext*) dhandle->handle;
	for(int i = 0; i < handle->total; ++i)
	{
		free(handle->namelist[i]);
	}
	if(handle->namelist) free(handle->namelist);
	if(handle->wildcard) free(handle->wildcard);
	free(handle);
#else
	if (dhandle->handle != NULL)
	{
		closedir(dhandle->handle);
	}
#endif

	dhandle->handle = NULL;
	dhandle->path[0] = '\0';
	dhandle->pattern[0] = '\0';
	dirlist_free(dhandle->entry_list);
	dhandle->current_entry = dhandle->entry_list = NULL;
	dhandle->entry_count = 0;
	dhandle->loaded = false;
}

void _tnfs_init_dhandle(dir_handle* dhandle, const char *path, uint8_t diropt, uint8_t sortopt, const char *pattern)
{
	time_t now = time(NULL);
	strlcpy(dhandle->path, path, MAX_TNFSPATH);
	if (pattern == NULL)
	{
		dhandle->pattern[0] = '\0';
	}
	else
	{
		strlcpy(dhandle->pattern, pattern, MAX_TNFSPATH);
	}
	dhandle->diropt = diropt;
	dhandle->sortopt = sortopt;
	dhandle->open_at = now;
}

int _tnfs_find_free_dir_handle(Session *s, const char *path, uint8_t diropt, uint8_t sortopt, const char *pattern, bool reuse)
{
	time_t now = time(NULL);

	// remove old handles
	for (int i = 0; i < MAX_DHND_PER_CONN; i++)
	{
		dir_handle *dhandle = &s->dhandles[i];
		if (dhandle->open)
		{
			continue;
		}
		if (dhandle->loaded && now > dhandle->open_at + DIR_HANDLE_TIMEOUT)
		{
#ifdef DEBUG
			fprintf(stderr, "freeing stale handle=%d\n", i);
#endif
			_tnfs_free_dir_handle(dhandle);
		}
	}

	// attempt 1 - try to reuse handle
	if (reuse)
	{
		for (int i = 0; i < MAX_DHND_PER_CONN; i++)
		{
			dir_handle *dhandle = &s->dhandles[i];
			if (dhandle->loaded
				&& strncmp(dhandle->path, path, MAX_TNFSPATH) == 0
				&& dhandle->diropt == diropt
				&& dhandle->sortopt == sortopt
				&& ((pattern == NULL && dhandle->pattern[0] == '\0') || strncmp(dhandle->pattern, pattern, MAX_TNFSPATH) == 0))
			{
#ifdef DEBUG
				fprintf(stderr, "reusing dir handle=%d\n", i);
#endif
				dhandle->current_entry = dhandle->entry_list;
				return i;
			}
		}
	}

	// attempt 2 - try to find empty handle
	for (int i = 0; i < MAX_DHND_PER_CONN; i++)
	{
		dir_handle *dhandle = &s->dhandles[i];
		if (dhandle->open || dhandle->loaded)
		{
			continue;
		}
#ifdef DEBUG
		fprintf(stderr, "allocating empty dir handle=%d\n", i);
#endif
		_tnfs_init_dhandle(dhandle, path, diropt, sortopt, pattern);
		return i;
	}

	// attempt 3 - try to find any available handle
	for (int i = 0; i < MAX_DHND_PER_CONN; i++)
	{
		dir_handle *dhandle = &s->dhandles[i];
		if (dhandle->open)
		{
			continue;
		}
#ifdef DEBUG
		fprintf(stderr, "allocating loaded dir handle=%d\n", i);
#endif
		if (dhandle->loaded)
		{
			_tnfs_free_dir_handle(dhandle);
		}
		_tnfs_init_dhandle(dhandle, path, diropt, sortopt, pattern);
		return i;
	}

	return -1;
}