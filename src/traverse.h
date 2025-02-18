#ifndef _TRAVERSE_H
#define _TRAVERSE_H

#include "tnfs.h"

int _traverse_directory(dir_handle *dirh, uint8_t diropts, uint8_t sortopts, uint16_t maxresults, const char *pattern);

#endif