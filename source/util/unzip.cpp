#include <minizip/unzip.h>
#include <algorithm>
#include <dirent.h>
#include <filesystem>
#include <string>
#include <cstring>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util/error.hpp"

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

int _extractFile(const char * path, unzFile unz, unz_file_info_s * fileInfo) {
    //check to make sure filepath or fileInfo isnt null
    if (path == NULL || fileInfo == NULL)
        return -1;

    if (unzOpenCurrentFile(unz) != UNZ_OK)
        return -2;

    std::string fullPath(path);
    auto slashPos = fullPath.find_last_of('/');
    if (slashPos != std::string::npos) {
        const std::string parent = fullPath.substr(0, slashPos);
        if (!_makeDirectoryParents(parent)) {
            LOG_DEBUG("could not create parent dir for %s\n", path);
            unzCloseCurrentFile(unz);
            return -5;
        }
    }
    
    u32 blocksize = 0x8000;
    u8 * buffer = (u8*) malloc(blocksize);
    if (buffer == NULL)
        return -3;
    u32 done = 0;
    int writeBytes = 0;
    FILE * fp = fopen(path, "w");
    if (fp == NULL) {
        free(buffer);
        return -4;		
    }
        
    while (done < fileInfo->uncompressed_size) {
        if (done + blocksize > fileInfo->uncompressed_size) {
            blocksize = fileInfo->uncompressed_size - done;
        }
        unzReadCurrentFile(unz, buffer, blocksize);
        writeBytes = write(fileno(fp), buffer, blocksize);
        if (writeBytes <= 0) {
            break;
        }
        done += writeBytes;
    }
    
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    
    free(buffer);
    if (done != fileInfo->uncompressed_size)
        return -4;		
    
    unzCloseCurrentFile(unz);
    return 0;
}

namespace inst::zip {
    bool extractFile(const std::string filename, const std::string destination) {
        unzFile unz = unzOpen(filename.c_str());

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
            } else {
                unz_file_pos pos;
                unzGetFilePos(unz, &pos);
            }

            unz_file_info_s * fileInfo = _getFileInfo(unz);

            std::string fileName = destination;
            fileName += _getFullFileName(unz, fileInfo);

            if (fileName.back() != '/') {
                int result = _extractFile(fileName.c_str(), unz, fileInfo);
                if (result < 0) {
                    free(fileInfo);
                    unzClose(unz);
                    return false;
                }
            }

            free(fileInfo);
        }

        if (i <= 0) {
            unzClose(unz);
            return false;
        }

        unzClose(unz);
        return true;
    }
}