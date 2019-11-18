/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "../common.h"
#include "../localisation/Language.h"
#include "IStream.hpp"
#include "String.hpp"

#include <algorithm>
#ifdef ENABLE_PHYSFS
#    include "Path.hpp"
#    include <physfs.h>
#endif

#ifndef _WIN32
#    include <sys/stat.h>
#endif

enum
{
    FILE_MODE_OPEN,
    FILE_MODE_WRITE,
    FILE_MODE_APPEND,
};

/**
 * A stream for reading and writing to files.
 */
class FileStream final : public IStream
{
private:
#ifdef ENABLE_PHYSFS
    PHYSFS_file* _file = nullptr;
    std::string _path;
#else
    FILE* _file = nullptr;
#endif
    bool _ownsFilePtr = false;
    bool _canRead = false;
    bool _canWrite = false;
    bool _disposed = false;
    uint64_t _fileSize = 0;

public:
    FileStream(const std::string& path, int32_t fileMode)
        : FileStream(path.c_str(), fileMode)
    {
    }

    FileStream(const utf8* path, int32_t fileMode)
    {
#if defined(ENABLE_PHYSFS)
        std::string path_str = std::string(path);
        size_t found = path_str.find("C:");
        if (found != std::string::npos)
        {
            path_str = path_str.replace(found, std::string("C:").length(), "");
        }

        Path::ConvertPathSlashes(path_str);

        switch (fileMode)
        {
            case FILE_MODE_OPEN:
                _file = PHYSFS_openRead(path_str.c_str());
                _canRead = true;
                _canWrite = false;
                break;
            case FILE_MODE_WRITE:
                _file = PHYSFS_openWrite(path_str.c_str());
                _canRead = false;
                _canWrite = true;
                break;
            case FILE_MODE_APPEND:
                _file = PHYSFS_openAppend(path);
                _canRead = false;
                _canWrite = true;
                break;
        }
        _path = path_str;
#else
        const char* mode;
        switch (fileMode)
        {
            case FILE_MODE_OPEN:
                mode = "rb";
                _canRead = true;
                _canWrite = false;
                break;
            case FILE_MODE_WRITE:
                mode = "w+b";
                _canRead = false;
                _canWrite = true;
                break;
            case FILE_MODE_APPEND:
                mode = "a";
                _canRead = false;
                _canWrite = true;
                break;
            default:
                throw;
        }
#   if defined(_WIN32)
        auto pathW = String::ToWideChar(path);
        auto modeW = String::ToWideChar(mode);
        _file = _wfopen(pathW.c_str(), modeW.c_str());
#   else
        if (fileMode == FILE_MODE_OPEN)
        {
            struct stat fileStat;
            // Only allow regular files to be opened as its possible to open directories.
            if (stat(path, &fileStat) == 0 && S_ISREG(fileStat.st_mode))
            {
                _file = fopen(path, mode);
            }
        }
        else
        {
            _file = fopen(path, mode);
        }
#   endif
#endif
        if (_file == nullptr)
        {
            throw IOException(String::StdFormat("Unable to open '%s'", path));
        }

        Seek(0, STREAM_SEEK_END);
        _fileSize = GetPosition();
        Seek(0, STREAM_SEEK_BEGIN);

        _ownsFilePtr = true;
    }

    ~FileStream() override
    {
#ifdef ENABLE_PHYSFS
        PHYSFS_close(_file);
        _disposed = true;
#else
        if (!_disposed)
        {
            _disposed = true;
            if (_ownsFilePtr)
            {
                fclose(_file);
            }
        }
#endif
    }

    bool CanRead() const override
    {
        return _canRead;
    }
    bool CanWrite() const override
    {
        return _canWrite;
    }

    uint64_t GetLength() const override
    {
        return _fileSize;
    }
    uint64_t GetPosition() const override
    {
#ifdef ENABLE_PHYSFS
        return PHYSFS_tell(_file);
#elif defined(_MSC_VER)
        return _ftelli64(_file);
#elif (defined(__APPLE__) && defined(__MACH__)) || defined(__ANDROID__) || defined(__OpenBSD__) || defined(__FreeBSD__)
        return ftello(_file);
#else
        return ftello64(_file);
#endif
    }

    void SetPosition(uint64_t position) override
    {
        Seek(position, STREAM_SEEK_BEGIN);
    }

    void Seek(int64_t offset, int32_t origin) override
    {
#ifdef ENABLE_PHYSFS
        switch (origin)
        {
            case STREAM_SEEK_BEGIN:
                PHYSFS_seek(_file, offset);
                break;
            case STREAM_SEEK_CURRENT:
                PHYSFS_seek(_file, offset + PHYSFS_tell(_file));
                break;
            case STREAM_SEEK_END:
                PHYSFS_seek(_file, PHYSFS_fileLength(_file) - offset);
                break;
        }
#elif defined(_MSC_VER)
        switch (origin)
        {
            case STREAM_SEEK_BEGIN:
                _fseeki64(_file, offset, SEEK_SET);
                break;
            case STREAM_SEEK_CURRENT:
                _fseeki64(_file, offset, SEEK_CUR);
                break;
            case STREAM_SEEK_END:
                _fseeki64(_file, offset, SEEK_END);
                break;
        }
#elif (defined(__APPLE__) && defined(__MACH__)) || defined(__ANDROID__) || defined(__OpenBSD__) || defined(__FreeBSD__)
        switch (origin)
        {
            case STREAM_SEEK_BEGIN:
                fseeko(_file, offset, SEEK_SET);
                break;
            case STREAM_SEEK_CURRENT:
                fseeko(_file, offset, SEEK_CUR);
                break;
            case STREAM_SEEK_END:
                fseeko(_file, offset, SEEK_END);
                break;
        }
#else
        switch (origin)
        {
            case STREAM_SEEK_BEGIN:
                fseeko64(_file, offset, SEEK_SET);
                break;
            case STREAM_SEEK_CURRENT:
                fseeko64(_file, offset, SEEK_CUR);
                break;
            case STREAM_SEEK_END:
                fseeko64(_file, offset, SEEK_END);
                break;
        }
#endif
    }

    void Read(void* buffer, uint64_t length) override
    {
        uint64_t remainingBytes = GetLength() - GetPosition();
        if (length <= remainingBytes)
        {
#ifdef ENABLE_PHYSFS
            // Workaround for physfs only allowing a file to be in
            // read or write mode at once
            if (!_canRead)
            {
                PHYSFS_sint32 previousPos = PHYSFS_tell(_file);
                // Close the existing file (write mode);
                PHYSFS_close(_file);
                // Open the file in read mode
                _file = PHYSFS_openRead(_path.c_str());
                PHYSFS_seek(_file, previousPos);
                // Perform the read
                bool readSuccess = false;
                if (PHYSFS_readBytes(_file, buffer, length) > 0)
                {
                    readSuccess = true;
                }
                // Close the read mode file regardless 
                // if successful
                previousPos = PHYSFS_tell(_file);
                PHYSFS_close(_file);
                _file = PHYSFS_openWrite(_path.c_str());
                PHYSFS_seek(_file, previousPos);
                // Skip the exception
                if (readSuccess)
                    return;
            }
            else 
            {
                if (PHYSFS_readBytes(_file, buffer, length) > 0)
                {
                    return;
                }
            }
#else
            if (fread(buffer, (size_t)length, 1, _file) == 1)
            {
                return;
            }
#endif
        }
        throw IOException("Attempted to read past end of file.");
    }

    void Write(const void* buffer, uint64_t length) override
    {
#ifdef ENABLE_PHYSFS
        if (PHYSFS_writeBytes(_file, buffer, length) < 0)
#else
        if (fwrite(buffer, (size_t)length, 1, _file) != 1)
#endif
        {
            throw IOException("Unable to write to file.");
        }

        uint64_t position = GetPosition();
        _fileSize = std::max(_fileSize, position);
    }

    uint64_t TryRead(void* buffer, uint64_t length) override
    {
#ifdef ENABLE_PHYSFS
        size_t readBytes = PHYSFS_readBytes(_file, buffer, length);
#else
        size_t readBytes = fread(buffer, 1, (size_t)length, _file);
#endif
        return readBytes;
    }

    const void* GetData() const override
    {
        return nullptr;
    }
};
