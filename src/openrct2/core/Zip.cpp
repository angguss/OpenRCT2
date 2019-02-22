/*****************************************************************************
 * Copyright (c) 2014-2018 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef __ANDROID__
#    include "Zip.h"

#    include "IStream.hpp"

#    include <zip.h>

class ZipArchive final : public IZipArchive
{
private:
    zip_t* _zip;
#ifdef ENABLE_PHYSFS
    PHYSFS_File* file;
#endif
    ZIP_ACCESS _access;
    std::vector<std::vector<uint8_t>> _writeBuffers;

public:
    ZipArchive(const std::string_view& path, ZIP_ACCESS access)
    {
        auto zipOpenMode = ZIP_RDONLY;
        if (access == ZIP_ACCESS::WRITE)
        {
            zipOpenMode = ZIP_CREATE;
        }
#ifdef ENABLE_PHYSFS
        if (access == ZIP_ACCESS::WRITE)
        {
            file = PHYSFS_openWrite(path.data());
        }
        else
        {
            file = PHYSFS_openRead(path.data());
        }

        PHYSFS_Stat stat;
        PHYSFS_stat(path.data(), &stat);
        if (access == ZIP_ACCESS::READ)
        {
            uint8_t* buf = new uint8_t[stat.filesize];
            PHYSFS_readBytes(file, buf, stat.filesize);
            zip_error_t er;
            zip_source_t* zipbuffer = zip_source_buffer_create(buf, stat.filesize, 1, &er);
            _zip = zip_open_from_source(zipbuffer, 0, &er);
        }
        else
        {
            int32_t error;
            _zip = zip_open(Path::Combine(PHYSFS_getWriteDir(), path.data()).c_str(), zipOpenMode, &error);
        }
#else
        int32_t error;
        _zip = zip_open(path.data(), zipOpenMode, &error);
#endif
        if (_zip == nullptr)
        {
            throw IOException("Unable to open zip file.");
        }

        _access = access;
    }

    ~ZipArchive() override
    {
        zip_close(_zip);
#ifdef ENABLE_PHYSFS
        PHYSFS_close(file);
#endif
    }

    size_t GetNumFiles() const override
    {
        return zip_get_num_entries(_zip, 0);
    }

    std::string GetFileName(size_t index) const override
    {
        std::string result;
        auto name = zip_get_name(_zip, index, ZIP_FL_ENC_GUESS);
        if (name != nullptr)
        {
            result = name;
        }
        return result;
    }

    uint64_t GetFileSize(size_t index) const override
    {
        zip_stat_t zipFileStat;
        if (zip_stat_index(_zip, index, 0, &zipFileStat) == ZIP_ER_OK)
        {
            return zipFileStat.size;
        }
        else
        {
            return 0;
        }
    }

    std::vector<uint8_t> GetFileData(const std::string_view& path) const override
    {
        std::vector<uint8_t> result;
        auto index = GetIndexFromPath(path);
        auto dataSize = GetFileSize(index);
        if (dataSize > 0 && dataSize < SIZE_MAX)
        {
            auto zipFile = zip_fopen_index(_zip, index, 0);
            if (zipFile != nullptr)
            {
                result.resize((size_t)dataSize);
                uint64_t readBytes = zip_fread(zipFile, result.data(), dataSize);
                if (readBytes != dataSize)
                {
                    result.clear();
                    result.shrink_to_fit();
                }
                zip_fclose(zipFile);
            }
        }
        return result;
    }

    void SetFileData(const std::string_view& path, std::vector<uint8_t>&& data) override
    {
        // Push buffer to an internal list as libzip requires access to it until the zip
        // handle is closed.
        _writeBuffers.push_back(std::move(data));
        const auto& writeBuffer = *_writeBuffers.rbegin();

        auto source = zip_source_buffer(_zip, writeBuffer.data(), writeBuffer.size(), 0);
        auto index = GetIndexFromPath(path);
        if (index == -1)
        {
            zip_add(_zip, path.data(), source);
        }
        else
        {
            zip_replace(_zip, index, source);
        }
    }

    void DeleteFile(const std::string_view& path) override
    {
        auto index = GetIndexFromPath(path);
        zip_delete(_zip, index);
    }

    void RenameFile(const std::string_view& path, const std::string_view& newPath) override
    {
        auto index = GetIndexFromPath(path);
        zip_file_rename(_zip, index, newPath.data(), ZIP_FL_ENC_GUESS);
    }

private:
    /**
     * Normalises both the given path and the stored paths and finds the first match.
     */
    zip_int64_t GetIndexFromPath(const std::string_view& path) const
    {
        auto normalisedPath = NormalisePath(path);
        if (!normalisedPath.empty())
        {
            auto numFiles = zip_get_num_entries(_zip, 0);
            for (zip_int64_t i = 0; i < numFiles; i++)
            {
                auto normalisedZipPath = NormalisePath(zip_get_name(_zip, i, ZIP_FL_ENC_GUESS));
                if (normalisedZipPath == normalisedPath)
                {
                    return i;
                }
            }
        }
        return -1;
    }

    static std::string NormalisePath(const std::string_view& path)
    {
        std::string result;
        if (!path.empty())
        {
            // Convert back slashes to forward slashes
            result = std::string(path);
            for (auto ch = result.data(); *ch != '\0'; ch++)
            {
                if (*ch == '\\')
                {
                    *ch = '/';
                }
            }
        }
        return result;
    }
};

namespace Zip
{
    std::unique_ptr<IZipArchive> Open(const std::string_view& path, ZIP_ACCESS access)
    {
        return std::make_unique<ZipArchive>(path, access);
    }

    std::unique_ptr<IZipArchive> TryOpen(const std::string_view& path, ZIP_ACCESS access)
    {
        std::unique_ptr<IZipArchive> result;
        try
        {
            result = std::make_unique<ZipArchive>(path, access);
        }
        catch (const std::exception&)
        {
        }
        return result;
    }
} // namespace Zip

#endif
