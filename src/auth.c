#include "auth.h"
#include "tnfs.h"
#include "tnfs_file.h"

int RW_CMDS[] =
{
    TNFS_MKDIR,
    TNFS_RMDIR,
    TNFS_WRITEBLOCK,
    TNFS_UNLINKFILE,
    TNFS_CHMODFILE,
    TNFS_RENAMEFILE,
    -1,
};

int RW_FLAGS =
    TNFS_O_WRONLY |
    TNFS_O_RDWR   |
    TNFS_O_APPEND |
    TNFS_O_CREAT  |
    TNFS_O_TRUNC  |
    TNFS_O_EXCL;

bool enable_write;

void auth_init(bool _enable_writes)
{
    enable_write = _enable_writes;
}

bool is_cmd_allowed(uint8_t cmd)
{
    if (enable_write)
    {
        return true;
    }
    for (int i = 0; RW_CMDS[i] != -1; i++)
    {
        if (RW_CMDS[i] == cmd)
        {
            return false;
        }
    }
    return true;
}

bool is_open_allowed(char *filename, int flags)
{
    if (enable_write)
    {
        return true;
    }
    return (flags & RW_FLAGS) == 0;
}
