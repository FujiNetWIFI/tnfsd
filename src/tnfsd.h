#ifndef _TNFSD_H
#define _TNFSD_H

#include <stdbool.h>
#include <stdio.h>

#define TNFSD_ERR_INVALID_DIR -1
#define TNFSD_ERR_SOCKET_ERROR -2

// Initialize the TNFS server. Should be called once before
// the server can be started.
void tnfsd_init();

// Initialize the logging subsystem to use the specified
// file descriptor. It'll use stderr by default.
void tnfsd_init_logs(int log_output_fd);

// Start the TNFS server. The function will block until
// the server is stopped with tnfsd_stop().
//
// In case of an error, it'll return immediate a negative
// value (see TNFSD_ERR_* constants).
int tnfsd_start(const char* path, int port, bool read_only);

// Stop the TNFS server and deallocate memory.
void tnfsd_stop(int sig);

#endif