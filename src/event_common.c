#include "event.h"

bool tnfs_event_is_active(event_wait_res_t* res, int fds)
{
    for (int i = 0; i < res->size; i++)
    {
        if (res->fds[i] == fds)
        {
            return true;
        }
    }
    return false;
}
