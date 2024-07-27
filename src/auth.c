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
    TNFS_O_APPEND |
    TNFS_O_CREAT  |
    TNFS_O_TRUNC  |
    TNFS_O_EXCL;

bool read_only;

void auth_init(bool _read_only)
{
    read_only = _read_only;
}

bool is_cmd_allowed(uint8_t cmd)
{
    if (!read_only)
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
    if (!read_only)
    {
        return true;
    }
    return (flags & RW_FLAGS) == 0;
}
