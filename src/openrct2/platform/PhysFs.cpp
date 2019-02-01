#ifdef __ENABLE_PHYSFS__

#    include "platform.h"

bool platform_file_exists_physfs(const utf8* path)
{
    return PHYSFS_exists(path);
}

bool platform_directory_exists_physfs(const utf8* path)
{
    if (!PHYSFS_exists(path))
        return false;

    PHYSFS_Stat stat;
    PHYSFS_stat(path, &stat);
    return stat.filetype == PHYSFS_FileType::PHYSFS_FILETYPE_DIRECTORY;
}

bool platform_ensure_directory_exists_physfs(const utf8* path)
{
    if (platform_directory_exists_physfs(path))
        return true;
    int success = PHYSFS_mkdir(path);
    return success == 1;
}

bool platform_directory_delete_physfs(const utf8* path)
{
    return PHYSFS_delete(path);
}

bool platform_file_delete_physfs(const utf8* path)
{
    return PHYSFS_delete(path);
}

#endif
