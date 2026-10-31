#ifndef NATIVE_IMAGE_H
#define NATIVE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "native_abi.h"

static const uint32_t NARM_MAGIC = 0x4d52414eu;
static const uint16_t NARM_HEADER_BYTES_V1 = 0x00a0u;
static const uint8_t NARM_FORMAT_MAJOR = 1;
static const uint8_t NARM_FORMAT_MINOR = 0;
static const uint32_t NARM_MACHINE_ARMV8M_MAIN_THUMB2 = 0x00080001u;
static const uint32_t NARM_RELOCATION_BYTES = 12u;
static const uint32_t NARM_IMPORT_BYTES = 8u;
static const uint32_t NARM_CRC32_OFFSET = 0x6cu;

enum NarmImageFlag : uint32_t
{
  NARM_FLAG_POSITION_INDEPENDENT = 1u << 0,
  NARM_FLAG_USES_FPU = 1u << 1,
  NARM_FLAG_COOPERATIVE_SAFEPOINTS = 1u << 2,
  NARM_FLAG_HAS_CRC32 = 1u << 3,
  NARM_FLAG_TRUSTED_PRIVILEGED = 1u << 4,
  NARM_FLAG_BACKGROUND_DEFAULT = 1u << 5,
  NARM_FLAG_V1_MASK = 0x3fu
};

enum NarmRelocationType : uint16_t
{
  NARM_RELOC_NONE = 0,
  NARM_RELOC_DATA_PTR32 = 1,
  NARM_RELOC_TEXT_PTR32 = 2,
  NARM_RELOC_THUMB_FUNC32 = 3,
  NARM_RELOC_CONTEXT_PTR32 = 4
};

enum NarmStatus : int32_t
{
  NARM_OK = 0,
  NARM_ERR_IO = -1,
  NARM_ERR_TRUNCATED = -2,
  NARM_ERR_MAGIC = -3,
  NARM_ERR_FORMAT = -4,
  NARM_ERR_MACHINE = -5,
  NARM_ERR_ABI = -6,
  NARM_ERR_API = -7,
  NARM_ERR_CAPABILITY = -8,
  NARM_ERR_SERVICE = -9,
  NARM_ERR_FLAGS = -10,
  NARM_ERR_LAYOUT = -11,
  NARM_ERR_ALIGNMENT = -12,
  NARM_ERR_SIZE = -13,
  NARM_ERR_CRC = -14,
  NARM_ERR_RELOCATION = -15,
  NARM_ERR_NO_PROCESS_SLOT = -16,
  NARM_ERR_NO_MEMORY = -17,
  NARM_ERR_CACHE = -18,
  NARM_ERR_FAULT = -19,
  NARM_ERR_BUSY = -20
};

struct NarmDiagnostic
{
  NarmStatus code;
  uint32_t field_offset;
  uint32_t detail;
};

typedef bool (*NarmReadAtFn)(void *user, uint32_t offset, void *destination, uint32_t bytes);

struct NarmReader
{
  void *user;
  uint32_t file_bytes;
  NarmReadAtFn read_at;
};

typedef bool (*NarmServiceSupportedFn)(void *user,
                                       uint32_t service_id,
                                       uint16_t minimum_version);

struct NarmValidationPolicy
{
  uint32_t machine;
  uint16_t abi_major;
  uint16_t abi_minor;
  uint16_t api_major;
  uint16_t api_minor;
  uint32_t supported_flags;
  uint32_t required_flags;
  uint32_t capabilities_low;
  uint32_t capabilities_high;
  uint32_t max_file_bytes;
  uint32_t max_text_bytes;
  uint32_t max_data_arena_bytes; // 0 = no policy cap; allocator/overflow checks remain.
  uint32_t max_stack_bytes;
  uint32_t max_heap_bytes;
  uint32_t max_alignment;
  uint32_t max_relocations;
  uint32_t max_imports;
  uint32_t max_name_bytes;
  void *service_user;
  NarmServiceSupportedFn service_supported;
};

struct NarmImageHeader
{
  uint32_t magic;
  uint16_t header_bytes;
  uint8_t format_major;
  uint8_t format_minor;
  uint16_t abi_major;
  uint16_t abi_minor;
  uint32_t machine;
  uint32_t flags;
  uint32_t file_bytes;
  uint32_t entry_text_offset;
  uint32_t text_file_offset;
  uint32_t text_bytes;
  uint32_t text_alignment;
  uint32_t rodata_file_offset;
  uint32_t rodata_bytes;
  uint32_t rodata_alignment;
  uint32_t data_file_offset;
  uint32_t data_bytes;
  uint32_t data_alignment;
  uint32_t bss_bytes;
  uint32_t bss_alignment;
  uint32_t stack_bytes;
  uint32_t heap_bytes;
  uint32_t reloc_file_offset;
  uint32_t reloc_count;
  uint32_t import_file_offset;
  uint32_t import_count;
  uint16_t api_major;
  uint16_t api_minor;
  uint32_t capabilities_low;
  uint32_t capabilities_high;
  uint32_t crc32;
  uint8_t build_id[16];
  uint32_t name_file_offset;
  uint32_t name_bytes;
};

struct NarmMemoryLayout
{
  uint32_t rodata_runtime_offset;
  uint32_t data_runtime_offset;
  uint32_t bss_runtime_offset;
  uint32_t heap_runtime_offset;
  uint32_t data_arena_bytes;
  uint32_t total_allocation_bytes;
};

struct NarmRelocation
{
  uint32_t patch_data_offset;
  uint32_t target_addend;
  uint16_t type;
};

struct NarmImport
{
  uint32_t service_id;
  uint16_t minimum_service_version;
  uint16_t flags;
};

NarmValidationPolicy narm_default_validation_policy();
const char *narm_status_string(NarmStatus status);

bool narm_read_exact(const NarmReader &reader,
                     uint32_t offset,
                     void *destination,
                     uint32_t bytes);

NarmStatus narm_decode_header(const NarmReader &reader,
                              NarmImageHeader &header,
                              NarmDiagnostic *diagnostic);

NarmStatus narm_validate_image(const NarmReader &reader,
                               const NarmValidationPolicy &policy,
                               NarmImageHeader &header,
                               NarmMemoryLayout &layout,
                               NarmDiagnostic *diagnostic);

NarmStatus narm_read_relocation(const NarmReader &reader,
                                const NarmImageHeader &header,
                                uint32_t index,
                                NarmRelocation &relocation);

NarmStatus narm_read_import(const NarmReader &reader,
                            const NarmImageHeader &header,
                            uint32_t index,
                            NarmImport &import_entry);

uint32_t narm_calculate_crc32(const NarmReader &reader, bool *read_ok);

#endif
