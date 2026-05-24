#include <minizip/unzip.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <string>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util/error.hpp"
#include "util/unzip.hpp"

// https://github.com/AtlasNX/Kosmos-Updater/blob/master/source/FileManager.cpp

unz_file_info_s * _getFileInfo(unzFile unz) {
    unz_file_info_s * fileInfo = (unz_file_info_s*) malloc(sizeof(unz_file_info_s));
    unzGetCurrentFileInfo(unz, fileInfo, NULL, 0, NULL, 0, NULL, 0);
    return fileInfo;
}

std::string _getFullFileName(unzFile unz, unz_file_info_s * fileInfo) {
    // Buffer must be size_filename+1 so minizip can write size_filename bytes
    // and we can safely append the terminating null. Passing only size_filename
    // as the buffer-size arg means minizip writes no trailing null — we add it.
    char filePath[fileInfo->size_filename + 1];

    unzGetCurrentFileInfo(unz, fileInfo, filePath, fileInfo->size_filename, NULL, 0, NULL, 0);
    filePath[fileInfo->size_filename] = '\0';

    return std::string(filePath);
}

bool _makeDirectoryParents(const std::string& path)
{
    // std::filesystem::create_directories handles every edge case the old
    // hand-rolled recursion got wrong: missing intermediate dirs, EEXIST,
    // trailing slashes, paths that include the sdmc: device prefix.
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        LOG_DEBUG("create_directories(%s) failed: %s\n", path.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

// Reasons returned by _extractFile so the caller can build a useful diagnostic.
enum class ExtractStage {
    Ok = 0,
    BadArgs,
    OpenInZip,
    MakeParents,
    AllocBuffer,
    Fopen,
    ReadFromZip,
    WriteToDisk,
    ShortWrite,
};

static int _extractFile(const char * path, unzFile unz, unz_file_info_s * fileInfo, ExtractStage * stageOut) {
    auto setStage = [&](ExtractStage s){ if (stageOut) *stageOut = s; };

    if (path == NULL || fileInfo == NULL) { setStage(ExtractStage::BadArgs); return -1; }

    if (unzOpenCurrentFile(unz) != UNZ_OK) { setStage(ExtractStage::OpenInZip); return -2; }

    std::string fullPath(path);
    auto slashPos = fullPath.find_last_of('/');
    if (slashPos != std::string::npos) {
        const std::string parent = fullPath.substr(0, slashPos);
        if (!_makeDirectoryParents(parent)) {
            LOG_DEBUG("could not create parent dir for %s\n", path);
            unzCloseCurrentFile(unz);
            setStage(ExtractStage::MakeParents);
            return -5;
        }
    }

    u32 blocksize = 0x8000;
    u8 * buffer = (u8*) malloc(blocksize);
    if (buffer == NULL) { unzCloseCurrentFile(unz); setStage(ExtractStage::AllocBuffer); return -3; }

    // Best-effort: drop any pre-existing target so fopen("wb") creates a fresh
    // file instead of trying to truncate an existing one.
    {
        std::error_code rmEc;
        std::filesystem::remove(path, rmEc);
    }

    FILE * fp = fopen(path, "wb");
    if (fp == NULL) {
        const int saved = errno;
        free(buffer);
        unzCloseCurrentFile(unz);
        setStage(ExtractStage::Fopen);
        LOG_DEBUG("fopen(%s) failed: errno=%d (%s)\n", path, saved, std::strerror(saved));
        (void)saved;
        return -4;
    }

    u64 done = 0;
    bool readErr = false;
    bool writeErr = false;
    while (done < fileInfo->uncompressed_size) {
        u32 want = blocksize;
        if (done + want > fileInfo->uncompressed_size) {
            want = fileInfo->uncompressed_size - done;
        }
        const int readBytes = unzReadCurrentFile(unz, buffer, want);
        if (readBytes <= 0) { readErr = true; break; }
        const int writeBytes = write(fileno(fp), buffer, readBytes);
        if (writeBytes <= 0) { writeErr = true; break; }
        done += writeBytes;
        if (writeBytes < readBytes) { writeErr = true; break; }
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    free(buffer);
    unzCloseCurrentFile(unz);

    if (readErr || writeErr || done != fileInfo->uncompressed_size) {
        // Roll back the partial write so we don't leave garbage on disk.
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (readErr) { setStage(ExtractStage::ReadFromZip); return -6; }
        if (writeErr) { setStage(ExtractStage::WriteToDisk); return -7; }
        setStage(ExtractStage::ShortWrite);
        return -4;
    }

    setStage(ExtractStage::Ok);
    return 0;
}

static const char * _stageName(ExtractStage s) {
    switch (s) {
        case ExtractStage::Ok: return "ok";
        case ExtractStage::BadArgs: return "bad_args";
        case ExtractStage::OpenInZip: return "open_in_zip";
        case ExtractStage::MakeParents: return "make_parents";
        case ExtractStage::AllocBuffer: return "alloc_buffer";
        case ExtractStage::Fopen: return "fopen";
        case ExtractStage::ReadFromZip: return "read_from_zip";
        case ExtractStage::WriteToDisk: return "write_to_disk";
        case ExtractStage::ShortWrite: return "short_write";
    }
    return "?";
}

namespace inst::zip {
    bool extractFile(const std::string filename, const std::string destination, std::string* errorOut) {
        unzFile unz = unzOpen(filename.c_str());
        if (unz == NULL) {
            if (errorOut) *errorOut = "unzOpen failed (corrupt download?)";
            return false;
        }

        int i = 0;
        for (;;) {
            int code;
            if (i == 0) {
                code = unzGoToFirstFile(unz);
            } else {
                code = unzGoToNextFile(unz);
            }
            i++;

            if (code == UNZ_END_OF_LIST_OF_FILE) {
                break;
            }

            unz_file_info_s * fileInfo = _getFileInfo(unz);

            std::string fileName = destination;
            fileName += _getFullFileName(unz, fileInfo);

            if (fileName.back() != '/') {
                ExtractStage stage = ExtractStage::Ok;
                int result = _extractFile(fileName.c_str(), unz, fileInfo, &stage);
                if (result < 0) {
                    if (errorOut) {
                        const int saved = errno;
                        *errorOut = std::string("stage=") + _stageName(stage)
                                  + " rc=" + std::to_string(result)
                                  + " errno=" + std::to_string(saved)
                                  + " path=" + fileName;
                    }
                    free(fileInfo);
                    unzClose(unz);
                    return false;
                }
            }

            free(fileInfo);
        }

        if (i <= 0) {
            unzClose(unz);
            if (errorOut) *errorOut = "empty zip";
            return false;
        }

        unzClose(unz);
        return true;
    }
}
