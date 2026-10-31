#include "file_io_runtime.h"

#include "SDmgr.h"
#include "globals.h"
#include "usb_storage_mode.h"

#include <FatFS.h>
#include <limits.h>
#include <string.h>

namespace
{
static constexpr size_t FILE_IO_PATH_BYTES = 256u;

bool valid_storage(uint32_t storage)
{
  return storage == FILE_IO_FLASH || storage == FILE_IO_SD;
}

bool valid_path(const char *path)
{
  return path && path[0] != 0 && strnlen(path, FILE_IO_PATH_BYTES) < FILE_IO_PATH_BYTES;
}

const char *normalized_path(uint32_t storage, const char *path,
                            char (&scratch)[FILE_IO_PATH_BYTES])
{
  if (!valid_path(path)) return nullptr;
  if (path[0] == '/') return path;
  if (storage == FILE_IO_SD && path[0] == '.' && path[1] == '/') return path + 2;
  scratch[0] = '/';
  const size_t length = strlen(path);
  if (length + 2u > sizeof(scratch)) return nullptr;
  memcpy(scratch + 1, path, length + 1u);
  return scratch;
}

bool storage_ready(uint32_t storage)
{
  if (!valid_storage(storage)) return false;
  return storage == FILE_IO_FLASH ? flash_storage_begin() : sdEnsureReady() == 1;
}

const char *base_name(const char *path)
{
  if (!path) return "";
  const char *base = path;
  for (const char *scan = path; *scan; ++scan)
    if (*scan == '/' || *scan == '\\') base = scan + 1;
  return base;
}

int32_t copy_entry_name(const char *name, bool directory,
                        char *name_out, uint32_t name_capacity,
                        uint32_t &name_length_out, uint32_t &type_out)
{
  type_out = directory ? 1u : 0u;
  const size_t length = strlen(name);
  if (!name_out || name_capacity == 0u)
  {
    name_length_out = 0u;
    return length == 0u ? 1 : 2;
  }
  const size_t copied = length < (size_t)(name_capacity - 1u)
                          ? length : (size_t)(name_capacity - 1u);
  memcpy(name_out, name, copied);
  name_out[copied] = 0;
  name_length_out = (uint32_t)copied;
  return copied == length ? 1 : 2;
}
}

bool file_io_mount(uint32_t storage)
{
  return storage_ready(storage);
}

bool file_io_exists(uint32_t storage, const char *path)
{
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !storage_ready(storage)) return false;
  if (storage == FILE_IO_FLASH) return FatFS.exists(normalized);
  SdBusGuard guard;
  return SD.exists(normalized);
}

bool file_io_create(uint32_t storage, const char *path)
{
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !storage_ready(storage)) return false;
  if (storage == FILE_IO_FLASH)
  {
    File file = FatFS.open(normalized, "w");
    const bool result = (bool)file;
    file.close();
    return result;
  }
  SdBusGuard guard;
  FirmwareFile file = SD.open(normalized, O_RDWR | O_CREAT | O_TRUNC);
  const bool result = (bool)file;
  file.close();
  return result;
}

bool file_io_remove(uint32_t storage, const char *path)
{
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !storage_ready(storage)) return false;
  if (storage == FILE_IO_FLASH) return FatFS.remove(normalized);
  SdBusGuard guard;
  return SD.remove(normalized);
}

bool file_io_rename(uint32_t storage, const char *from, const char *to)
{
  char from_scratch[FILE_IO_PATH_BYTES];
  char to_scratch[FILE_IO_PATH_BYTES];
  const char *normalized_from = normalized_path(storage, from, from_scratch);
  const char *normalized_to = normalized_path(storage, to, to_scratch);
  if (!normalized_from || !normalized_to || !storage_ready(storage)) return false;
  if (storage == FILE_IO_FLASH) return FatFS.rename(normalized_from, normalized_to);
  SdBusGuard guard;
  return SD.rename(normalized_from, normalized_to);
}

int32_t file_io_size(uint32_t storage, const char *path, uint32_t &size_out)
{
  size_out = 0u;
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !valid_storage(storage)) return FILE_IO_ERR_ARGUMENTS;
  if (!storage_ready(storage)) return FILE_IO_ERR_STORAGE;
  if (storage == FILE_IO_FLASH)
  {
    File file = FatFS.open(normalized, "r");
    if (!file) return FILE_IO_ERR_NOT_FOUND;
    const size_t size = file.size();
    file.close();
    if (size > UINT32_MAX) return FILE_IO_ERR_IO;
    size_out = (uint32_t)size;
    return 0;
  }
  SdBusGuard guard;
  FirmwareFile file = SD.open(normalized, FIRMWARE_FILE_READ);
  if (!file) return FILE_IO_ERR_NOT_FOUND;
  const uint64_t size = file.fileSize();
  file.close();
  if (size > UINT32_MAX) return FILE_IO_ERR_IO;
  size_out = (uint32_t)size;
  return 0;
}

int32_t file_io_read_at(uint32_t storage, const char *path,
                        uint32_t file_offset, void *destination,
                        uint32_t byte_count)
{
  if (byte_count > INT32_MAX || (byte_count != 0u && !destination))
    return FILE_IO_ERR_ARGUMENTS;
  if (byte_count == 0u) return 0;
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !valid_storage(storage)) return FILE_IO_ERR_ARGUMENTS;
  if (!storage_ready(storage)) return FILE_IO_ERR_STORAGE;
  if (storage == FILE_IO_FLASH)
  {
    File file = FatFS.open(normalized, "r");
    if (!file) return FILE_IO_ERR_NOT_FOUND;
    if (!file.seek(file_offset)) { file.close(); return FILE_IO_ERR_SEEK; }
    const int bytes = file.read((uint8_t *)destination, byte_count);
    file.close();
    return bytes < 0 ? FILE_IO_ERR_IO : bytes;
  }
  SdBusGuard guard;
  FirmwareFile file = SD.open(normalized, FIRMWARE_FILE_READ);
  if (!file) return FILE_IO_ERR_NOT_FOUND;
  if (!file.seekSet(file_offset)) { file.close(); return FILE_IO_ERR_SEEK; }
  const int bytes = file.read(destination, byte_count);
  file.close();
  return bytes < 0 ? FILE_IO_ERR_IO : bytes;
}

int32_t file_io_write_at(uint32_t storage, const char *path,
                         uint32_t file_offset, const void *source,
                         uint32_t byte_count)
{
  if (byte_count > INT32_MAX || (byte_count != 0u && !source))
    return FILE_IO_ERR_ARGUMENTS;
  if (byte_count == 0u) return 0;
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !valid_storage(storage)) return FILE_IO_ERR_ARGUMENTS;
  if (!storage_ready(storage)) return FILE_IO_ERR_STORAGE;
  if (storage == FILE_IO_FLASH)
  {
    File file = FatFS.open(normalized, "r+");
    if (!file) file = FatFS.open(normalized, "w+");
    if (!file) return FILE_IO_ERR_OPEN;
    if (!file.seek(file_offset)) { file.close(); return FILE_IO_ERR_SEEK; }
    const size_t bytes = file.write((const uint8_t *)source, byte_count);
    file.close();
    return bytes > INT32_MAX ? FILE_IO_ERR_IO : (int32_t)bytes;
  }
  SdBusGuard guard;
  FirmwareFile file = SD.open(normalized, O_RDWR | O_CREAT);
  if (!file) return FILE_IO_ERR_OPEN;
  if (!file.seekSet(file_offset)) { file.close(); return FILE_IO_ERR_SEEK; }
  const size_t bytes = file.write(source, byte_count);
  file.close();
  return bytes > INT32_MAX ? FILE_IO_ERR_IO : (int32_t)bytes;
}

bool file_io_mkdir(uint32_t storage, const char *path)
{
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !storage_ready(storage)) return false;
  if (storage == FILE_IO_FLASH) return FatFS.mkdir(normalized);
  SdBusGuard guard;
  return SD.mkdir(normalized);
}

bool file_io_rmdir(uint32_t storage, const char *path)
{
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !storage_ready(storage)) return false;
  if (storage == FILE_IO_FLASH) return FatFS.rmdir(normalized);
  SdBusGuard guard;
  return SD.rmdir(normalized);
}

int32_t file_io_dir_entry(uint32_t storage, const char *path,
                          uint32_t entry_index, char *name_out,
                          uint32_t name_capacity, uint32_t &name_length_out,
                          uint32_t &type_out)
{
  name_length_out = 0u;
  type_out = 0u;
  if (name_capacity != 0u && !name_out) return FILE_IO_ERR_ARGUMENTS;
  char scratch[FILE_IO_PATH_BYTES];
  const char *normalized = normalized_path(storage, path, scratch);
  if (!normalized || !valid_storage(storage)) return FILE_IO_ERR_ARGUMENTS;
  if (!storage_ready(storage)) return FILE_IO_ERR_STORAGE;
  if (storage == FILE_IO_FLASH)
  {
    FSStat stat = {};
    if (!FatFS.stat(normalized, &stat) || !stat.isDir) return 0;
    Dir dir = FatFS.openDir(normalized);
    for (uint32_t index = 0; dir.next(); ++index)
    {
      if (index == entry_index)
      {
        const String entry_name = dir.fileName();
        return copy_entry_name(base_name(entry_name.c_str()),
                                               dir.isDirectory(), name_out,
                                               name_capacity, name_length_out,
                                               type_out);
      }
    }
    return 0;
  }
  SdBusGuard guard;
  FirmwareFile dir = SD.open(normalized, FIRMWARE_FILE_READ);
  if (!dir || !dir.isDir()) { if (dir) dir.close(); return 0; }
  for (uint32_t index = 0;; ++index)
  {
    FirmwareFile entry;
    entry.openNext(&dir, FIRMWARE_FILE_READ);
    if (!entry) { dir.close(); return 0; }
    if (index == entry_index)
    {
      char entry_name[FILE_IO_PATH_BYTES] = {0};
      entry.getName(entry_name, sizeof(entry_name));
      const int32_t result = copy_entry_name(base_name(entry_name), entry.isDir(),
                                             name_out, name_capacity,
                                             name_length_out, type_out);
      entry.close();
      dir.close();
      return result;
    }
    entry.close();
  }
}
