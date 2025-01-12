#include <stdio.h>

#include "auth.h"
#include "datagram.h"
#include "directory.h"
#include "errortable.h"
#include "event.h"
#include "log.h"
#include "version.h"
#include "tnfsd.h"

void tnfsd_init()
{
	tnfs_init();              /* initialize structures etc. */
	tnfs_init_errtable();     /* initialize error lookup table */
}

void tnfsd_init_logs(int log_output_fd)
{
    FILE* log_output = fdopen(log_output_fd, "w");
    log_init(log_output);
}

int tnfsd_start(const char* path, int port, bool read_only)
{
	LOG("Starting tnfsd version %s on port %d using root directory \"%s\"\n", version, port, path);
    if (read_only)
    {
        LOG("The server runs in read-only mode. TNFS clients can only list and download files.\n");
    }
    else
    {
        LOG("The server runs in read-write mode. TNFS clients can upload and modify files. Use -r to enable read-only mode.\n");
    }

    if (tnfs_setroot(path) < 0)
    {
		LOG("Invalid root directory: %s\n", path);
        return TNFSD_ERR_INVALID_DIR;
    }
	tnfs_event_init();        /* initialize event system */
	if (tnfs_sockinit(port) < 0)  /* initialize communications */
    {
        LOG("Can't bind port %d\n", port);
        return TNFSD_ERR_SOCKET_ERROR;
    }      
	auth_init(read_only);     /* initialize authentication */
	tnfs_mainloop();          /* run */
    tnfs_event_close();
    return 0;
}

void tnfsd_stop()
{
    LOG("Stopping tnfsd server.\n");
    tnfs_sockclose();
}
