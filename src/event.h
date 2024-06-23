#ifndef _EVENT_H
#define _EVENT_H

#include <stdbool.h>

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

struct event_wait_res
{
    int *fds;
    int size;
};
typedef struct event_wait_res event_wait_res_t;

// Initializes the event queue.
void tnfs_event_init();

// Registers the file descriptor to watch.
bool tnfs_event_register(int fd);

// Unregisters the file descriptor.
void tnfs_event_unregister(int fd);

// Waits for a given amount of time for an event on any registered file descriptor.
// Returns the file descriptor number of 0 if timeout occurs.
event_wait_res_t* tnfs_event_wait(int timeout_sec);

// Returns 
bool tnfs_event_is_active(event_wait_res_t* res, int fds);

// Closes the event queue.
void tnfs_event_close();

#endif