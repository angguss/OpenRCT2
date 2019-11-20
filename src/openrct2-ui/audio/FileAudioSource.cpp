/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "AudioContext.h"
#include "AudioFormat.h"

#include <SDL.h>
#include <algorithm>
#include <openrct2/audio/AudioSource.h>
#include <openrct2/common.h>

#if defined (ENABLE_PHYSFS)
#   include <physfs.h>
#endif

namespace OpenRCT2::Audio
{
    /**
     * An audio source where raw PCM data is streamed directly from
     * a file.
     */
    class FileAudioSource final : public ISDLAudioSource
    {
    private:
        AudioFormat _format = {};
#if defined(ENABLE_PHYSFS)
        PHYSFS_File* _rw = nullptr;
#else
        SDL_RWops* _rw = nullptr;
#endif
        uint64_t _dataBegin = 0;
        uint64_t _dataLength = 0;

    public:
        ~FileAudioSource()
        {
            Unload();
        }

#if defined(ENABLE_PHYSFS)
        int64_t tell(PHYSFS_File* rw) 
        {
            return PHYSFS_tell(rw);
        }

        // Mode is ignored in physfs
        int64_t seek(PHYSFS_File* rw, int64_t offset, int mode)
        {
            if (mode == RW_SEEK_CUR)
            {
                uint64_t curPos = tell(rw);
                offset = curPos + offset;
            }
            return PHYSFS_seek(rw, offset);
        }

        size_t read(PHYSFS_File* rw, void* dst, size_t bytesToRead) 
        {
            return PHYSFS_readBytes(rw, dst, bytesToRead);
        }

        uint32_t readLE32(PHYSFS_File* rw) 
        {
            uint32_t val;
            PHYSFS_readULE32(rw, &val);
            return val;
        }

        void close(PHYSFS_File* rw) 
        {
            PHYSFS_close(rw);
        }
#else
        int64_t tell(SDL_RWops* rw) 
        {
            return SDL_RWtell(rw);
        }

        int64_t seek(SDL_RWops* rw, int64_t offset, int mode)
        {
            return SDL_RWseek(rw, dataOffset, mode);
        }

        size_t read(SDL_RWops* rw, void* dst, size_t bytesToRead) 
        {
            return SDL_RWread(rw, dst, 1, bytesToRead);
        }

        uint32_t readLE32(SDL_RWops* rw)
        {
            return SDL_ReadLE32(rw);
        }

        void close(SDL_RWops* rw) 
        {
            SDL_RWclose(rw);
        }
#endif
        uint64_t GetLength() const override
        {
            return _dataLength;
        }

        AudioFormat GetFormat() const override
        {
            return _format;
        }

        size_t Read(void* dst, uint64_t offset, size_t len) override
        {
            size_t bytesRead = 0;
            int64_t currentPosition = tell(_rw);
            if (currentPosition != -1)
            {
                size_t bytesToRead = (size_t)std::min<uint64_t>(len, _dataLength - offset);
                int64_t dataOffset = _dataBegin + offset;
                if (currentPosition != dataOffset)
                {
                    int64_t newPosition = seek(_rw, dataOffset, SEEK_SET);
                    if (newPosition == -1)
                    {
                        return 0;
                    }
                }
                bytesRead = read(_rw, dst, bytesToRead);
            }
            return bytesRead;
        }

#if defined(ENABLE_PHYSFS)
        bool LoadWAV(PHYSFS_File* rw)
#else
        bool LoadWAV(SDL_RWops* rw)
#endif
        {
            const uint32_t DATA = 0x61746164;
            const uint32_t FMT = 0x20746D66;
            const uint32_t RIFF = 0x46464952;
            const uint32_t WAVE = 0x45564157;
            const uint16_t pcmformat = 0x0001;

            Unload();

            if (rw == nullptr)
            {
                return false;
            }
            _rw = rw;

            uint32_t chunkId = readLE32(rw);
            if (chunkId != RIFF)
            {
                log_verbose("Not a WAV file");
                return false;
            }

            // Read and discard chunk size
            readLE32(rw);
            uint32_t chunkFormat = readLE32(rw);
            if (chunkFormat != WAVE)
            {
                log_verbose("Not in WAVE format");
                return false;
            }

            uint32_t fmtChunkSize = FindChunk(rw, FMT);
            if (!fmtChunkSize)
            {
                log_verbose("Could not find FMT chunk");
                return false;
            }

            uint64_t chunkStart = tell(rw);

            WaveFormat waveFormat;
            read(rw, &waveFormat, sizeof(waveFormat));
            seek(rw, chunkStart + fmtChunkSize, RW_SEEK_SET);
            if (waveFormat.encoding != pcmformat)
            {
                log_verbose("Not in proper format");
                return false;
            }

            _format.freq = waveFormat.frequency;
            switch (waveFormat.bitspersample)
            {
                case 8:
                    _format.format = AUDIO_U8;
                    break;
                case 16:
                    _format.format = AUDIO_S16LSB;
                    break;
                default:
                    log_verbose("Invalid bits per sample");
                    return false;
            }
            _format.channels = waveFormat.channels;

            uint32_t dataChunkSize = FindChunk(rw, DATA);
            if (dataChunkSize == 0)
            {
                log_verbose("Could not find DATA chunk");
                return false;
            }

            _dataLength = dataChunkSize;
            _dataBegin = tell(rw);
            return true;
        }

    private:
#ifdef ENABLE_PHYSFS
        uint32_t FindChunk(PHYSFS_File* rw, uint32_t wantedId)
#else
        uint32_t FindChunk(SDL_RWops* rw, uint32_t wantedId)
#endif
        {
            uint32_t subchunkId = readLE32(rw);
            uint32_t subchunkSize = readLE32(rw);
            if (subchunkId == wantedId)
            {
                return subchunkSize;
            }
            const uint32_t FACT = 0x74636166;
            const uint32_t LIST = 0x5453494c;
            const uint32_t BEXT = 0x74786562;
            const uint32_t JUNK = 0x4B4E554A;
            while (subchunkId == FACT || subchunkId == LIST || subchunkId == BEXT || subchunkId == JUNK)
            {
                seek(rw, subchunkSize, RW_SEEK_CUR);
                subchunkId = readLE32(rw);
                subchunkSize = readLE32(rw);
                if (subchunkId == wantedId)
                {
                    return subchunkSize;
                }
            }
            return 0;
        }

        void Unload()
        {
            if (_rw != nullptr)
            {
                close(_rw);
                _rw = nullptr;
            }
            _dataBegin = 0;
            _dataLength = 0;
        }
    };

#if defined(ENABLE_PHYSFS)
    IAudioSource* AudioSource::CreateStreamFromWAV(const std::string& path)
    {
        IAudioSource* source = nullptr;
        PHYSFS_File* rw = PHYSFS_openRead(path.c_str());
        if (rw != nullptr)
        {
            return AudioSource::CreateStreamFromWAV(rw);
        }
        return source;
    }

    IAudioSource* AudioSource::CreateStreamFromWAV(PHYSFS_File* rw)
    {
        auto source = new FileAudioSource();
        if (!source->LoadWAV(rw))
        {
            delete source;
            source = nullptr;
        }
        return source;
    }
#else
    IAudioSource* AudioSource::CreateStreamFromWAV(const std::string& path)
    {
        IAudioSource* source = nullptr;
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (rw != nullptr)
        {
            return AudioSource::CreateStreamFromWAV(rw);
        }
        return source;
    }

    IAudioSource* AudioSource::CreateStreamFromWAV(SDL_RWops* rw)
    {
        auto source = new FileAudioSource();
        if (!source->LoadWAV(rw))
        {
            delete source;
            source = nullptr;
        }
        return source;
    }
#endif
} // namespace OpenRCT2::Audio
