#ifndef FILE_IO_RUNTIME_H
#define FILE_IO_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

enum FileIoStorage : uint32_t
{
  FILE_IO_FLASH = 0,
  // Kept so existing NARM programs and firmware code continue to compile.
  FILE_IO_LITTLEFS = FILE_IO_FLASH,
  FILE_IO_SD = 1
};

enum FileIoResult : int32_t
{
  FILE_IO_ERR_ARGUMENTS = -1,
  FILE_IO_ERR_STORAGE = -2,
  FILE_IO_ERR_NOT_FOUND = -3,
  FILE_IO_ERR_OPEN = -4,
  FILE_IO_ERR_SEEK = -5,
  FILE_IO_ERR_IO = -6
};

bool file_io_mount(uint32_t storage);
bool file_io_exists(uint32_t storage, const char *path);
bool file_io_create(uint32_t storage, const char *path);
bool file_io_remove(uint32_t storage, const char *path);
bool file_io_rename(uint32_t storage, const char *from, const char *to);
int32_t file_io_size(uint32_t storage, const char *path, uint32_t &size_out);
int32_t file_io_read_at(uint32_t storage, const char *path,
                        uint32_t file_offset, void *destination,
                        uint32_t byte_count);
int32_t file_io_write_at(uint32_t storage, const char *path,
                         uint32_t file_offset, const void *source,
                         uint32_t byte_count);
bool file_io_mkdir(uint32_t storage, const char *path);
bool file_io_rmdir(uint32_t storage, const char *path);

// Returns 1 for a complete name, 2 for a truncated name, 0 when the indexed
// entry does not exist, or a negative FileIoResult. type_out: file=0, dir=1.
int32_t file_io_dir_entry(uint32_t storage, const char *path,
                          uint32_t entry_index, char *name_out,
                          uint32_t name_capacity, uint32_t &name_length_out,
                          uint32_t &type_out);

#endif
