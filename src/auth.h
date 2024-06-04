#ifndef _READONLY_H
#define _READONLY_H

#include <stdint.h>
#include <stdbool.h>

void auth_init(bool enable_writes);

bool is_cmd_allowed(uint8_t cmd);

bool is_open_allowed(char *filename, int flags);

#endif
