#ifndef BSD
#define _XOPEN_SOURCE 500
#endif

#include <ctype.h>
#include <errno.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdcompat.h"
#include "directory.h"
#include "fileinfo.h"
#include "match.h"
#include "traverse.h"

// Unfortunately, nftw() doesn't support passing custom parameters, so we need to use
// global variables. Luckily, this is a single-threaded server.
char _path[MAX_TNFSPATH];
uint8_t _diropts;
uint16_t _maxresults;
const char *_pattern;

/* ignore patterns loaded from .ignore in the root of traversal */
char **_ignore_patterns = NULL;
int _ignore_count = 0;

static void _free_traverse_ignore(void)
{
	if (_ignore_patterns)
	{
		for (int i = 0; i < _ignore_count; ++i)
			free(_ignore_patterns[i]);
		free(_ignore_patterns);
		_ignore_patterns = NULL;
		_ignore_count = 0;
	}
}

static void _load_traverse_ignore(const char *path)
{
	char ignorepath[MAX_TNFSPATH + 16];
	char line[1024];
	FILE *f;

	_free_traverse_ignore();
	snprintf(ignorepath, sizeof(ignorepath), "%s/.ignore", path);
	f = fopen(ignorepath, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f) != NULL)
	{
		char *s = line;
		while (*s && (*s == ' ' || *s == '\t')) s++;
		char *e = s + strlen(s);
		while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) e--;
		*e = '\0';
		if (*s == '\0' || *s == '#')
			continue;
		char **tmp = realloc(_ignore_patterns, sizeof(char *) * (_ignore_count + 1));
		if (!tmp) break;
		_ignore_patterns = tmp;
		_ignore_patterns[_ignore_count] = strdup(s);
		if (_ignore_patterns[_ignore_count])
			_ignore_count++;
	}
	fclose(f);
}

// A list to hold all subdirectory names
directory_entry_list _list_dirs = NULL;

// A list to hold all normal file names
directory_entry_list _list_files = NULL;
uint16_t _entrycount = 0;

int _nftwfunc(const char *fullpath, const struct stat *statptr, int fileflags, struct FTW *pfwt)
{
	const char *filename;
	const char *filename_with_slash;
	if (strlen(fullpath) > (strlen(_path)+1))
	{
		filename_with_slash = fullpath + strlen(_path);
		filename = filename_with_slash + 1;
	}
	else
	{
		return 0;
	}

	// Try to stat the file before we can decide on other things
	fileinfo_t finf;
	if (get_fileinfo(fullpath, &finf) == 0)
	{

		/* If it's not a directory and we have a pattern that this doesn't match, skip it
			Ignore the directory qualification if TNFS_DIROPT_DIR_PATTERN is set */
		if ((_diropts & TNFS_DIROPT_DIR_PATTERN) || !(finf.flags & FILEINFOFLAG_DIRECTORY))
		{
			if (_pattern != NULL && gitignore_glob_match(filename_with_slash, _pattern) == false)
				return 0;
		}

		/* skip entries that match .ignore patterns loaded for this traversal */
		if (_ignore_count > 0)
		{
			for (int ip = 0; ip < _ignore_count; ++ip)
			{
				if (gitignore_glob_match(filename_with_slash, _ignore_patterns[ip]) || gitignore_glob_match(filename, _ignore_patterns[ip]))
					return 0;
			}
		}

		// Skip this if it's hidden (assuming TNFS_DIROPT_NO_SKIPHIDDEN isn't set)
		if (!(_diropts & TNFS_DIROPT_NO_SKIPHIDDEN) && (finf.flags & FILEINFOFLAG_HIDDEN))
			return 0;

		// Skip this if it's special (assuming TNFS_DIROPT_NO_SKIPSPECIAL isn't set)
		if (!(_diropts & TNFS_DIROPT_NO_SKIPSPECIAL) && (finf.flags & FILEINFOFLAG_SPECIAL))
			return 0;

		if ((_diropts & TNFS_DIROPT_NO_FOLDERS) && (finf.flags & FILEINFOFLAG_DIRECTORY))
			return 0;

		// Create a new directory_entry_node to add to our list
		directory_entry_list_node *node = calloc(1, sizeof(directory_entry_list_node));

		// Copy the name into the node
		strlcpy(node->entry.entrypath, filename, MAX_FILENAME_LEN);

		directory_entry_list *list_dest_p = &_list_files;

		if (finf.flags & FILEINFOFLAG_DIRECTORY)
		{
			node->entry.flags = finf.flags;
			/* If the TNFS_DIROPT_NO_FOLDERSFIRST 0x01  flag hasn't been set, put this node
				in a separate list for directories so they're sorted separately */
			if (!(_diropts & TNFS_DIROPT_NO_FOLDERSFIRST))
				list_dest_p = &_list_dirs;
		}
		node->entry.size = finf.size;
		node->entry.mtime = finf.m_time;
		node->entry.ctime = finf.c_time;

		dirlist_push(list_dest_p, node);
		_entrycount++;

		// If we were given a max, break if we've reached it
		if (_maxresults > 0 && _entrycount >= _maxresults)
			return 1;
#ifdef DEBUG
		//fprintf(stderr, "_load_directory added \"%s\" %u\n", node->entry.entrypath, node->entry.size);
#endif
	}
	return 0;
}

int _traverse_directory(dir_handle *dirh, uint8_t diropts, uint8_t sortopts, uint16_t maxresults, const char *pattern)
{
	// Free any existing entries
	dirlist_free(dirh->entry_list);
	dirh->entry_count = 0;

	strncpy(_path, dirh->path, sizeof(_path));
    int len = strlen(_path);
	if (len > 0 && _path[len-1] == '/')
	{
		_path[len-1] = '\0';
	}

	_diropts = diropts | TNFS_DIROPT_DIR_PATTERN;
	_maxresults = maxresults;
	_pattern = pattern;

	// A list to hold all subdirectory names
	_list_dirs = NULL;
	// A list to hold all normal file names
	_list_files = NULL;
	_entrycount = 0;

	int fd_limit = 5;
	int flags = FTW_CHDIR | FTW_DEPTH | FTW_MOUNT;

	/* load .ignore patterns for this traversal root */
	_load_traverse_ignore(_path);

	if (nftw(_path, _nftwfunc, fd_limit, flags) == -1)
	{
		_free_traverse_ignore();
		return errno;
	}

	// Sort the two lists (assuming TNFS_DIRSORT_NONE isn't set)
	if (!(sortopts & TNFS_DIRSORT_NONE))
	{
		if (_list_dirs != NULL)
			dirlist_sort(&_list_dirs, sortopts);
		if (_list_files != NULL)
			dirlist_sort(&_list_files, sortopts);
	}

	// Combine the two lists into one
	dirh->entry_list = dirlist_concat(_list_dirs, _list_files);
	dirh->entry_count = _entrycount;

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

	_free_traverse_ignore();

	return 0;
}
