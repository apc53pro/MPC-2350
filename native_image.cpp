#include "native_image.h"

#include <limits.h>
#include <string.h>

namespace
{
uint16_t read_le16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t read_le32(const uint8_t *p)
{
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

NarmStatus fail(NarmDiagnostic *diagnostic,
                NarmStatus status,
                uint32_t field_offset,
                uint32_t detail)
{
  if (diagnostic)
  {
    diagnostic->code = status;
    diagnostic->field_offset = field_offset;
    diagnostic->detail = detail;
  }
  return status;
}

bool add_u32(uint32_t a, uint32_t b, uint32_t &out)
{
  if (a > UINT32_MAX - b)
  {
    return false;
  }
  out = a + b;
  return true;
}

bool multiply_u32(uint32_t a, uint32_t b, uint32_t &out)
{
  if (a != 0 && b > UINT32_MAX / a)
  {
    return false;
  }
  out = a * b;
  return true;
}

bool is_power_of_two(uint32_t value)
{
  return value != 0 && (value & (value - 1u)) == 0;
}

bool align_up_u32(uint32_t value, uint32_t alignment, uint32_t &out)
{
  if (!is_power_of_two(alignment))
  {
    return false;
  }

  uint32_t mask = alignment - 1u;
  if (value > UINT32_MAX - mask)
  {
    return false;
  }

  out = (value + mask) & ~mask;
  return true;
}

struct FileRange
{
  uint32_t begin;
  uint32_t end;
  uint32_t field_offset;
};

bool add_file_range(FileRange *ranges,
                    uint32_t &range_count,
                    uint32_t begin,
                    uint32_t bytes,
                    uint32_t header_bytes,
                    uint32_t file_bytes,
                    uint32_t field_offset,
                    NarmDiagnostic *diagnostic)
{
  if (bytes == 0)
  {
    return true;
  }

  uint32_t end = 0;
  if (begin < header_bytes || !add_u32(begin, bytes, end) || end > file_bytes)
  {
    fail(diagnostic, NARM_ERR_LAYOUT, field_offset, bytes);
    return false;
  }

  for (uint32_t i = 0; i < range_count; ++i)
  {
    if (begin < ranges[i].end && ranges[i].begin < end)
    {
      fail(diagnostic, NARM_ERR_LAYOUT, field_offset, ranges[i].field_offset);
      return false;
    }
  }

  ranges[range_count].begin = begin;
  ranges[range_count].end = end;
  ranges[range_count].field_offset = field_offset;
  ++range_count;
  return true;
}

bool validate_utf8_name(const NarmReader &reader,
                        uint32_t offset,
                        uint32_t bytes,
                        NarmDiagnostic *diagnostic)
{
  uint32_t codepoint = 0;
  uint32_t minimum = 0;
  uint8_t remaining = 0;

  for (uint32_t i = 0; i < bytes; ++i)
  {
    uint8_t ch = 0;
    if (!narm_read_exact(reader, offset + i, &ch, 1))
    {
      fail(diagnostic, NARM_ERR_IO, 0x80u, i);
      return false;
    }

    if (remaining == 0)
    {
      if (ch == 0)
      {
        fail(diagnostic, NARM_ERR_FORMAT, 0x84u, i);
        return false;
      }
      if (ch <= 0x7fu)
      {
        continue;
      }
      if (ch >= 0xc2u && ch <= 0xdfu)
      {
        remaining = 1;
        minimum = 0x80u;
        codepoint = ch & 0x1fu;
      }
      else if (ch >= 0xe0u && ch <= 0xefu)
      {
        remaining = 2;
        minimum = 0x800u;
        codepoint = ch & 0x0fu;
      }
      else if (ch >= 0xf0u && ch <= 0xf4u)
      {
        remaining = 3;
        minimum = 0x10000u;
        codepoint = ch & 0x07u;
      }
      else
      {
        fail(diagnostic, NARM_ERR_FORMAT, 0x84u, i);
        return false;
      }
    }
    else
    {
      if ((ch & 0xc0u) != 0x80u)
      {
        fail(diagnostic, NARM_ERR_FORMAT, 0x84u, i);
        return false;
      }

      codepoint = (codepoint << 6) | (ch & 0x3fu);
      --remaining;
      if (remaining == 0)
      {
        if (codepoint < minimum || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
        {
          fail(diagnostic, NARM_ERR_FORMAT, 0x84u, i);
          return false;
        }
      }
    }
  }

  if (remaining != 0)
  {
    fail(diagnostic, NARM_ERR_FORMAT, 0x84u, bytes);
    return false;
  }
  return true;
}

bool range_contains_word(uint32_t range_begin,
                         uint32_t range_bytes,
                         uint32_t word_offset)
{
  uint32_t range_end = 0;
  uint32_t word_end = 0;
  return add_u32(range_begin, range_bytes, range_end) &&
         add_u32(word_offset, 4u, word_end) &&
         word_offset >= range_begin && word_end <= range_end;
}
} // namespace

NarmValidationPolicy narm_default_validation_policy()
{
  NarmValidationPolicy policy = {};
  policy.machine = NARM_MACHINE_ARMV8M_MAIN_THUMB2;
  policy.abi_major = NARM_ABI_MAJOR;
  policy.abi_minor = NARM_ABI_MINOR;
  policy.api_major = NARM_API_MAJOR;
  policy.api_minor = NARM_API_MINOR;
  policy.supported_flags = NARM_FLAG_V1_MASK;
  policy.required_flags = NARM_FLAG_POSITION_INDEPENDENT |
                          NARM_FLAG_COOPERATIVE_SAFEPOINTS |
                          NARM_FLAG_HAS_CRC32 |
                          NARM_FLAG_TRUSTED_PRIVILEGED;
  policy.capabilities_low = 0;
  policy.capabilities_high = 0;
  policy.max_file_bytes = 8u * 1024u * 1024u;
  policy.max_text_bytes = 2u * 1024u * 1024u;
  policy.max_data_arena_bytes = 0u; // No default per-program data cap.
  policy.max_stack_bytes = 512u * 1024u;
  policy.max_heap_bytes = 2u * 1024u * 1024u;
  policy.max_alignment = 4096u;
  policy.max_relocations = 16384u;
  policy.max_imports = 1024u;
  policy.max_name_bytes = 255u;
  policy.service_user = nullptr;
  policy.service_supported = nullptr;
  return policy;
}

const char *narm_status_string(NarmStatus status)
{
  switch (status)
  {
    case NARM_OK: return "ok";
    case NARM_ERR_IO: return "I/O error";
    case NARM_ERR_TRUNCATED: return "truncated image";
    case NARM_ERR_MAGIC: return "invalid magic";
    case NARM_ERR_FORMAT: return "unsupported image format";
    case NARM_ERR_MACHINE: return "wrong machine";
    case NARM_ERR_ABI: return "incompatible native ABI";
    case NARM_ERR_API: return "incompatible firmware API";
    case NARM_ERR_CAPABILITY: return "unsupported capability";
    case NARM_ERR_SERVICE: return "unsupported service";
    case NARM_ERR_FLAGS: return "invalid image flags";
    case NARM_ERR_LAYOUT: return "invalid image layout";
    case NARM_ERR_ALIGNMENT: return "invalid alignment";
    case NARM_ERR_SIZE: return "size limit exceeded";
    case NARM_ERR_CRC: return "CRC mismatch";
    case NARM_ERR_RELOCATION: return "invalid relocation";
    case NARM_ERR_NO_PROCESS_SLOT: return "no native process slot";
    case NARM_ERR_NO_MEMORY: return "not enough native memory";
    case NARM_ERR_CACHE: return "cache maintenance failed";
    case NARM_ERR_FAULT: return "native process fault";
    case NARM_ERR_BUSY: return "native runtime busy";
    default: return "unknown native runtime error";
  }
}

bool narm_read_exact(const NarmReader &reader,
                     uint32_t offset,
                     void *destination,
                     uint32_t bytes)
{
  uint32_t end = 0;
  if (!reader.read_at || !add_u32(offset, bytes, end) || end > reader.file_bytes)
  {
    return false;
  }
  return bytes == 0 || reader.read_at(reader.user, offset, destination, bytes);
}

NarmStatus narm_decode_header(const NarmReader &reader,
                              NarmImageHeader &header,
                              NarmDiagnostic *diagnostic)
{
  if (diagnostic)
  {
    diagnostic->code = NARM_OK;
    diagnostic->field_offset = 0;
    diagnostic->detail = 0;
  }

  if (reader.file_bytes < NARM_HEADER_BYTES_V1)
  {
    return fail(diagnostic, NARM_ERR_TRUNCATED, 0x04u, reader.file_bytes);
  }

  uint8_t raw[NARM_HEADER_BYTES_V1];
  if (!narm_read_exact(reader, 0, raw, sizeof(raw)))
  {
    return fail(diagnostic, NARM_ERR_IO, 0, 0);
  }

  memset(&header, 0, sizeof(header));
  header.magic = read_le32(raw + 0x00);
  header.header_bytes = read_le16(raw + 0x04);
  header.format_major = raw[0x06];
  header.format_minor = raw[0x07];
  header.abi_major = read_le16(raw + 0x08);
  header.abi_minor = read_le16(raw + 0x0a);
  header.machine = read_le32(raw + 0x0c);
  header.flags = read_le32(raw + 0x10);
  header.file_bytes = read_le32(raw + 0x14);
  header.entry_text_offset = read_le32(raw + 0x18);
  header.text_file_offset = read_le32(raw + 0x1c);
  header.text_bytes = read_le32(raw + 0x20);
  header.text_alignment = read_le32(raw + 0x24);
  header.rodata_file_offset = read_le32(raw + 0x28);
  header.rodata_bytes = read_le32(raw + 0x2c);
  header.rodata_alignment = read_le32(raw + 0x30);
  header.data_file_offset = read_le32(raw + 0x34);
  header.data_bytes = read_le32(raw + 0x38);
  header.data_alignment = read_le32(raw + 0x3c);
  header.bss_bytes = read_le32(raw + 0x40);
  header.bss_alignment = read_le32(raw + 0x44);
  header.stack_bytes = read_le32(raw + 0x48);
  header.heap_bytes = read_le32(raw + 0x4c);
  header.reloc_file_offset = read_le32(raw + 0x50);
  header.reloc_count = read_le32(raw + 0x54);
  header.import_file_offset = read_le32(raw + 0x58);
  header.import_count = read_le32(raw + 0x5c);
  header.api_major = read_le16(raw + 0x60);
  header.api_minor = read_le16(raw + 0x62);
  header.capabilities_low = read_le32(raw + 0x64);
  header.capabilities_high = read_le32(raw + 0x68);
  header.crc32 = read_le32(raw + 0x6c);
  memcpy(header.build_id, raw + 0x70, sizeof(header.build_id));
  header.name_file_offset = read_le32(raw + 0x80);
  header.name_bytes = read_le32(raw + 0x84);

  if (header.magic != NARM_MAGIC)
  {
    return fail(diagnostic, NARM_ERR_MAGIC, 0x00u, header.magic);
  }

  for (uint32_t i = 0x88u; i < NARM_HEADER_BYTES_V1; ++i)
  {
    if (raw[i] != 0)
    {
      return fail(diagnostic, NARM_ERR_FORMAT, i, raw[i]);
    }
  }

  return NARM_OK;
}

NarmStatus narm_read_relocation(const NarmReader &reader,
                                const NarmImageHeader &header,
                                uint32_t index,
                                NarmRelocation &relocation)
{
  if (index >= header.reloc_count)
  {
    return NARM_ERR_RELOCATION;
  }

  uint32_t relative = 0;
  uint32_t offset = 0;
  if (!multiply_u32(index, NARM_RELOCATION_BYTES, relative) ||
      !add_u32(header.reloc_file_offset, relative, offset))
  {
    return NARM_ERR_RELOCATION;
  }

  uint8_t raw[NARM_RELOCATION_BYTES];
  if (!narm_read_exact(reader, offset, raw, sizeof(raw)))
  {
    return NARM_ERR_IO;
  }

  relocation.patch_data_offset = read_le32(raw + 0);
  relocation.target_addend = read_le32(raw + 4);
  relocation.type = read_le16(raw + 8);
  if (read_le16(raw + 10) != 0)
  {
    return NARM_ERR_RELOCATION;
  }
  return NARM_OK;
}

NarmStatus narm_read_import(const NarmReader &reader,
                            const NarmImageHeader &header,
                            uint32_t index,
                            NarmImport &import_entry)
{
  if (index >= header.import_count)
  {
    return NARM_ERR_SERVICE;
  }

  uint32_t relative = 0;
  uint32_t offset = 0;
  if (!multiply_u32(index, NARM_IMPORT_BYTES, relative) ||
      !add_u32(header.import_file_offset, relative, offset))
  {
    return NARM_ERR_SERVICE;
  }

  uint8_t raw[NARM_IMPORT_BYTES];
  if (!narm_read_exact(reader, offset, raw, sizeof(raw)))
  {
    return NARM_ERR_IO;
  }

  import_entry.service_id = read_le32(raw + 0);
  import_entry.minimum_service_version = read_le16(raw + 4);
  import_entry.flags = read_le16(raw + 6);
  return NARM_OK;
}

uint32_t narm_calculate_crc32(const NarmReader &reader, bool *read_ok)
{
  if (read_ok)
  {
    *read_ok = false;
  }

  uint32_t crc = 0xffffffffu;
  uint8_t buffer[256];
  uint32_t offset = 0;

  while (offset < reader.file_bytes)
  {
    uint32_t remaining = reader.file_bytes - offset;
    uint32_t chunk = remaining < sizeof(buffer) ? remaining : (uint32_t)sizeof(buffer);
    if (!narm_read_exact(reader, offset, buffer, chunk))
    {
      return 0;
    }

    for (uint32_t i = 0; i < chunk; ++i)
    {
      uint32_t absolute = offset + i;
      uint8_t value = (absolute >= NARM_CRC32_OFFSET &&
                       absolute < NARM_CRC32_OFFSET + 4u) ? 0u : buffer[i];
      crc ^= value;
      for (uint32_t bit = 0; bit < 8; ++bit)
      {
        uint32_t mask = 0u - (crc & 1u);
        crc = (crc >> 1) ^ (0xedb88320u & mask);
      }
    }
    offset += chunk;
  }

  if (read_ok)
  {
    *read_ok = true;
  }
  return crc ^ 0xffffffffu;
}

NarmStatus narm_validate_image(const NarmReader &reader,
                               const NarmValidationPolicy &policy,
                               NarmImageHeader &header,
                               NarmMemoryLayout &layout,
                               NarmDiagnostic *diagnostic)
{
  NarmStatus status = narm_decode_header(reader, header, diagnostic);
  if (status != NARM_OK)
  {
    return status;
  }

  if (header.header_bytes < NARM_HEADER_BYTES_V1 ||
      header.header_bytes > reader.file_bytes)
  {
    return fail(diagnostic, NARM_ERR_FORMAT, 0x04u, header.header_bytes);
  }
  if (header.format_major != NARM_FORMAT_MAJOR)
  {
    return fail(diagnostic, NARM_ERR_FORMAT, 0x06u, header.format_major);
  }
  if (header.machine != policy.machine)
  {
    return fail(diagnostic, NARM_ERR_MACHINE, 0x0cu, header.machine);
  }
  if (header.abi_major != policy.abi_major || header.abi_minor > policy.abi_minor)
  {
    return fail(diagnostic, NARM_ERR_ABI, 0x08u,
                ((uint32_t)header.abi_major << 16) | header.abi_minor);
  }
  if (header.api_major != policy.api_major || header.api_minor > policy.api_minor)
  {
    return fail(diagnostic, NARM_ERR_API, 0x60u,
                ((uint32_t)header.api_major << 16) | header.api_minor);
  }
  if ((header.flags & ~policy.supported_flags) != 0 ||
      (header.flags & policy.required_flags) != policy.required_flags)
  {
    return fail(diagnostic, NARM_ERR_FLAGS, 0x10u, header.flags);
  }
  if ((header.capabilities_low & ~policy.capabilities_low) != 0 ||
      (header.capabilities_high & ~policy.capabilities_high) != 0)
  {
    return fail(diagnostic, NARM_ERR_CAPABILITY, 0x64u,
                header.capabilities_low & ~policy.capabilities_low);
  }
  if (header.file_bytes != reader.file_bytes ||
      header.file_bytes > policy.max_file_bytes)
  {
    return fail(diagnostic, NARM_ERR_SIZE, 0x14u, header.file_bytes);
  }

  const uint32_t alignments[4] = {
    header.text_alignment,
    header.rodata_alignment,
    header.data_alignment,
    header.bss_alignment
  };
  const uint32_t alignment_fields[4] = {0x24u, 0x30u, 0x3cu, 0x44u};
  for (uint32_t i = 0; i < 4; ++i)
  {
    if (!is_power_of_two(alignments[i]) || alignments[i] > policy.max_alignment)
    {
      return fail(diagnostic, NARM_ERR_ALIGNMENT, alignment_fields[i], alignments[i]);
    }
  }
  if (header.text_alignment < 8u)
  {
    return fail(diagnostic, NARM_ERR_ALIGNMENT, 0x24u, header.text_alignment);
  }
  if (header.text_bytes == 0 || (header.text_bytes & 1u) != 0 ||
      header.text_bytes > policy.max_text_bytes ||
      header.entry_text_offset >= header.text_bytes ||
      (header.entry_text_offset & 1u) != 0)
  {
    return fail(diagnostic, NARM_ERR_SIZE, 0x20u, header.text_bytes);
  }
  if (header.stack_bytes < 256u || (header.stack_bytes & 7u) != 0 ||
      header.stack_bytes > policy.max_stack_bytes ||
      header.heap_bytes > policy.max_heap_bytes)
  {
    return fail(diagnostic, NARM_ERR_SIZE, 0x48u, header.stack_bytes);
  }
  if (header.reloc_count > policy.max_relocations ||
      header.import_count > policy.max_imports ||
      header.name_bytes > policy.max_name_bytes)
  {
    return fail(diagnostic, NARM_ERR_SIZE, 0x54u, header.reloc_count);
  }

  memset(&layout, 0, sizeof(layout));
  layout.rodata_runtime_offset = 0;
  if (!align_up_u32(header.rodata_bytes, header.data_alignment,
                    layout.data_runtime_offset))
  {
    return fail(diagnostic, NARM_ERR_LAYOUT, 0x38u, header.data_bytes);
  }
  uint32_t data_end = 0;
  if (!add_u32(layout.data_runtime_offset, header.data_bytes, data_end) ||
      !align_up_u32(data_end, header.bss_alignment, layout.bss_runtime_offset))
  {
    return fail(diagnostic, NARM_ERR_LAYOUT, 0x40u, header.bss_bytes);
  }
  uint32_t bss_end = 0;
  if (!add_u32(layout.bss_runtime_offset, header.bss_bytes, bss_end) ||
      !align_up_u32(bss_end, 8u, layout.heap_runtime_offset) ||
      !add_u32(layout.heap_runtime_offset, header.heap_bytes, layout.data_arena_bytes))
  {
    return fail(diagnostic, NARM_ERR_LAYOUT, 0x4cu, header.heap_bytes);
  }
  if (policy.max_data_arena_bytes != 0u &&
      layout.data_arena_bytes > policy.max_data_arena_bytes)
  {
    return fail(diagnostic, NARM_ERR_SIZE, 0x40u, layout.data_arena_bytes);
  }
  uint32_t text_and_data = 0;
  if (!add_u32(header.text_bytes, layout.data_arena_bytes, text_and_data) ||
      !add_u32(text_and_data, header.stack_bytes, layout.total_allocation_bytes))
  {
    return fail(diagnostic, NARM_ERR_SIZE, 0x14u, UINT32_MAX);
  }

  uint32_t reloc_bytes = 0;
  uint32_t import_bytes = 0;
  if (!multiply_u32(header.reloc_count, NARM_RELOCATION_BYTES, reloc_bytes) ||
      !multiply_u32(header.import_count, NARM_IMPORT_BYTES, import_bytes))
  {
    return fail(diagnostic, NARM_ERR_LAYOUT, 0x54u, header.reloc_count);
  }

  FileRange ranges[6] = {};
  uint32_t range_count = 0;
  if (!add_file_range(ranges, range_count, header.text_file_offset,
                      header.text_bytes, header.header_bytes, header.file_bytes,
                      0x1cu, diagnostic) ||
      !add_file_range(ranges, range_count, header.rodata_file_offset,
                      header.rodata_bytes, header.header_bytes, header.file_bytes,
                      0x28u, diagnostic) ||
      !add_file_range(ranges, range_count, header.data_file_offset,
                      header.data_bytes, header.header_bytes, header.file_bytes,
                      0x34u, diagnostic) ||
      !add_file_range(ranges, range_count, header.reloc_file_offset,
                      reloc_bytes, header.header_bytes, header.file_bytes,
                      0x50u, diagnostic) ||
      !add_file_range(ranges, range_count, header.import_file_offset,
                      import_bytes, header.header_bytes, header.file_bytes,
                      0x58u, diagnostic) ||
      !add_file_range(ranges, range_count, header.name_file_offset,
                      header.name_bytes, header.header_bytes, header.file_bytes,
                      0x80u, diagnostic))
  {
    return diagnostic ? diagnostic->code : NARM_ERR_LAYOUT;
  }

  if (!validate_utf8_name(reader, header.name_file_offset, header.name_bytes, diagnostic))
  {
    return diagnostic ? diagnostic->code : NARM_ERR_FORMAT;
  }

  bool crc_read_ok = false;
  uint32_t actual_crc = narm_calculate_crc32(reader, &crc_read_ok);
  if (!crc_read_ok)
  {
    return fail(diagnostic, NARM_ERR_IO, NARM_CRC32_OFFSET, 0);
  }
  if (actual_crc != header.crc32)
  {
    return fail(diagnostic, NARM_ERR_CRC, NARM_CRC32_OFFSET, actual_crc);
  }

  for (uint32_t i = 0; i < header.import_count; ++i)
  {
    NarmImport required = {};
    status = narm_read_import(reader, header, i, required);
    if (status != NARM_OK || required.flags != 0 || required.service_id > 0xffffu ||
        !policy.service_supported ||
        !policy.service_supported(policy.service_user,
                                  required.service_id,
                                  required.minimum_service_version))
    {
      return fail(diagnostic, status == NARM_ERR_IO ? NARM_ERR_IO : NARM_ERR_SERVICE,
                  0x58u, i);
    }
  }

  for (uint32_t i = 0; i < header.reloc_count; ++i)
  {
    NarmRelocation relocation = {};
    status = narm_read_relocation(reader, header, i, relocation);
    if (status != NARM_OK || (relocation.patch_data_offset & 3u) != 0)
    {
      return fail(diagnostic, status == NARM_ERR_IO ? NARM_ERR_IO : NARM_ERR_RELOCATION,
                  0x50u, i);
    }

    bool patch_in_data = range_contains_word(layout.data_runtime_offset,
                                             header.data_bytes,
                                             relocation.patch_data_offset);
    bool patch_in_bss = range_contains_word(layout.bss_runtime_offset,
                                            header.bss_bytes,
                                            relocation.patch_data_offset);
    if (!patch_in_data && !patch_in_bss)
    {
      return fail(diagnostic, NARM_ERR_RELOCATION, 0x50u, i);
    }

    switch (relocation.type)
    {
      case NARM_RELOC_NONE:
        break;
      case NARM_RELOC_DATA_PTR32:
        if (relocation.target_addend >= layout.data_arena_bytes)
        {
          return fail(diagnostic, NARM_ERR_RELOCATION, 0x50u, i);
        }
        break;
      case NARM_RELOC_TEXT_PTR32:
      case NARM_RELOC_THUMB_FUNC32:
        if (relocation.target_addend >= header.text_bytes ||
            (relocation.target_addend & 1u) != 0)
        {
          return fail(diagnostic, NARM_ERR_RELOCATION, 0x50u, i);
        }
        break;
      case NARM_RELOC_CONTEXT_PTR32:
        if (relocation.target_addend != 0)
        {
          return fail(diagnostic, NARM_ERR_RELOCATION, 0x50u, i);
        }
        break;
      default:
        return fail(diagnostic, NARM_ERR_RELOCATION, 0x50u, i);
    }
  }

  return NARM_OK;
}
