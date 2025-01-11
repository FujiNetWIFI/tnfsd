#ifndef _TNFSD_H
#define _TNFSD_H

#include <stdbool.h>

#define TNFSD_ERR_INVALID_DIR -1
#define TNFSD_ERR_SOCKET_ERROR -2

void tnfsd_init();
int tnfsd_start(const char* path, int port, bool read_only);
void tnfsd_stop();

#endif