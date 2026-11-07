#include "native_abi.h"
#include "native_live_diagnostics.h"
#include "native_gpu_records.h"
#include "nano_gpu.h"
#include <assert.h>

namespace {
NativeContextV1 owner{};
NanoGpu gpu;
bool focused = false;
bool fail_fence = false;
unsigned device_calls = 0;
uint32_t last_token = 0;
NanoGpu::FrameResult frame_result = NanoGpu::FrameBusy;
bool recovery_requested = false;

bool valid_service_arguments(NativeContextV1 *context, const uint32_t *args,
                             uint32_t words)
{
  return context == &owner && words <= 14u && (words == 0u || args);
}

void abort_if_stop_requested(NativeContextV1 *) {}
bool has_capability(NativeContextV1 *context, uint64_t capability)
{
  return (context->granted_capabilities_low & capability) != 0;
}
bool native_owns_gpu(NativeContextV1 *) { return focused; }
bool native_pointer_valid(NativeContextV1 *, const void *pointer,
                          uint32_t bytes, uint32_t alignment)
{
  const auto address = reinterpret_cast<uintptr_t>(pointer);
  return alignment == 1u && address >= 0x1000u && address < 0x1800u &&
         bytes <= 0x1800u - address;
}
NanoGpu &gpu_display_client() { return gpu; }
bool gpu_display_link_status(uint8_t *) { return true; }
bool gpu_display_abort() { recovery_requested = true; return true; }
bool gpu_display_begin() { return false; }
void native_fault_checkpoint(uint32_t) {}
constexpr uint32_t NARM_PHASE_VIDEO = 0x20000000u;

#include "native_gpu_api_impl.h"
}

bool NanoGpu::begin(NanoGpuTransport, const uint16_t (*)[16])
{
  online_ = true;
  return true;
}
bool NanoGpu::get_caps(uint8_t *) { ++device_calls; return true; }
bool NanoGpu::get_status(uint8_t *) { ++device_calls; return true; }
bool NanoGpu::write_heap(uint32_t, const uint8_t *, uint32_t) { ++device_calls; return true; }
bool NanoGpu::submit_pass(uint32_t) { ++device_calls; return true; }
bool NanoGpu::copy_rect(const uint32_t *) { ++device_calls; return true; }
bool NanoGpu::fence(uint32_t token)
{
  ++device_calls;
  last_token = token;
  if (fail_fence) online_ = false;
  return online_;
}
bool NanoGpu::present(uint32_t target, uint32_t token)
{
  ++device_calls;
  last_token = token;
  front_ = target;
  return true;
}
bool NanoGpu::read_memory(uint32_t, uint32_t, uint8_t *) { ++device_calls; return true; }
NanoGpu::FrameResult NanoGpu::frame_ready() { return frame_result; }
NanoGpu::FrameResult NanoGpu::try_present(uint32_t, uint32_t token)
{
  last_token = token;
  return frame_result;
}

void verify_records()
{
  constexpr uint32_t heap = NanoGpu::kUserHeap;
  uint8_t out[320];
  auto word = [&out](unsigned i) {
    const auto *p = out + 4 * i;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  };
  assert(native_gpu_encode_pass(out, 1, 7, 0xabcd, 0xffffff, heap));
  assert(word(0) == 0x3153504e && word(1) == 1 && word(2) == 7 &&
         word(3) == 0xabcd && word(4) == 0xffffff && word(5) == heap &&
         word(6) == 300 && word(7) == 0);
  assert(!native_gpu_encode_pass(out, 1, 8, 0, 0, heap));
  assert(!native_gpu_encode_pass(out, 1, 0, 0, 0, 0x800000u - 2396));
  assert(native_gpu_encode_tile(out, heap + 2400, 4) && word(0) == heap + 2400 && word(1) == 4);
  assert(!native_gpu_encode_tile(out, heap, 0));
  assert(native_gpu_encode_tile(out, 0, 0) && word(0) == 0 && word(1) == 0);

  NativeGpuPrimitiveInput p{};
  p.kind = 2;
  p.flags = 0x70000;
  p.depth_function = 1;
  p.rgb888 = 0x123456;
  p.depth = 0xfedcba;
  p.origin[0] = 5; p.origin[1] = 17;
  p.line_end[0] = 639; p.line_end[1] = 479;
  p.scissor[0] = -3; p.scissor[2] = 640; p.scissor[3] = 480;
  p.bounds[2] = 639; // Ignored for lines.
  assert(native_gpu_encode_primitive(out, p));
  assert(word(0) == 0x70102 && word(1) == 0x100 && word(2) == 0x123456);
  assert(word(4) == 0xfffffffdu && word(10) == 0 && word(16) == 0xfedcba);
  assert(word(17) == 5 && word(18) == 17 && word(19) == 0 && word(20) == 639 && word(21) == 479);
  for (unsigned i = 22; i < 80; ++i) assert(word(i) == 0);
  p.line_end[0] = 640;
  assert(!native_gpu_encode_primitive(out, p));

  p.kind = 0;
  p.flags |= 0x80000;
  p.shade = 4;
  p.addressing = 3;
  p.key_rgb565 = 0xf81f;
  p.texture_base = heap;
  p.texture_width = 16; p.texture_height = 32; p.texture_pitch = 64;
  p.vertices_q4[0] = -4;
  p.edges[0] = -100; p.edges[3] = 1;
  p.planes[0] = 0x01234567; p.planes[1] = 0xfedcba98; p.planes[41] = 0xdeadbeef;
  assert(native_gpu_encode_primitive(out, p));
  assert(word(0) == 0xf0100 && word(1) == 0x70104 && word(3) == 0xf81f);
  assert(word(12) == heap && word(13) == 16 && word(14) == 32 && word(15) == 64 && word(16) == 0);
  assert(word(20) == 0xfffffffcu && word(26) == 0xffffff9cu && word(29) == 1);
  assert(word(38) == 0x01234567 && word(39) == 0xfedcba98 && word(79) == 0xdeadbeef);
  assert(!native_gpu_encode_primitive(reinterpret_cast<uint8_t *>(&p), p));
  p.texture_pitch = 30;
  assert(!native_gpu_encode_primitive(out, p));
}

int main()
{
  verify_records();
  assert(narm_serial_command("GPU_STATUS") == NARM_SERIAL_NONE);
  assert(narm_serial_command("TASKS") == NARM_SERIAL_NONE);
  assert(narm_serial_command("NARMISH") == NARM_SERIAL_NONE);
  assert(narm_serial_command("nArM dIaG") == NARM_SERIAL_DIAG);
  assert(narm_serial_command("NARM CAPTURE TEST") == NARM_SERIAL_CAPTURE_TEST);
  assert(narm_serial_command("NARM UNKNOWN") == NARM_SERIAL_INVALID);

  uint32_t args[14] = {0x1001u};
  assert(native_gpu_submit(nullptr, NARM_GPU_GET_CAPS, args, 1) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_gpu_submit(&owner, NARM_GPU_GET_CAPS, args, 1) == NARM_SERVICE_ERR_CAPABILITY);
  owner.granted_capabilities_low = NARM_CAP_GPU;
  assert(native_gpu_submit(&owner, NARM_GPU_GET_CAPS, args, 1) == NARM_SERVICE_ERR_BUSY);
  focused = true;
  assert(native_gpu_submit(&owner, NARM_GPU_GET_CAPS, args, 1) == NARM_SERVICE_ERR_DEVICE);
  assert(native_gpu_submit(&owner, NARM_GPU_LINK_STATUS, args, 1) == NARM_SERVICE_OK);
  assert(native_gpu_submit(&owner, NARM_GPU_ABORT, nullptr, 0) == NARM_SERVICE_OK && recovery_requested);
  assert(native_gpu_submit(&owner, NARM_GPU_RESET, nullptr, 0) == NARM_SERVICE_ERR_DEVICE);
  assert(native_gpu_submit(&owner, NARM_GPU_ENCODE_PASS, args, 1) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(device_calls == 0);
  gpu.begin({}, nullptr);

  assert(native_gpu_submit(&owner, NARM_GPU_GET_CAPS, args, 1) == NARM_SERVICE_OK);
  args[0] = 0x17c1u;
  assert(native_gpu_submit(&owner, NARM_GPU_GET_STATUS, args, 1) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] = 0x17c0u;
  assert(native_gpu_submit(&owner, NARM_GPU_GET_STATUS, args, 1) == NARM_SERVICE_OK);

  args[0] = NanoGpu::kUserHeap - 1u;
  args[1] = 0x1001u;
  args[2] = 956u;
  assert(native_gpu_submit(&owner, NARM_GPU_WRITE_HEAP, args, 3) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] = 0x800000u - args[2];
  assert(native_gpu_submit(&owner, NARM_GPU_WRITE_HEAP, args, 3) == NARM_SERVICE_OK);
  ++args[0];
  assert(native_gpu_submit(&owner, NARM_GPU_WRITE_HEAP, args, 3) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] = NanoGpu::kUserHeap;
  args[2] = 957u;
  assert(native_gpu_submit(&owner, NARM_GPU_WRITE_HEAP, args, 3) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] += 1u;
  assert(native_gpu_submit(&owner, NARM_GPU_SUBMIT_PASS, args, 1) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] -= 1u;
  assert(native_gpu_submit(&owner, NARM_GPU_SUBMIT_PASS, args, 1) == NARM_SERVICE_OK);

  args[2] = 0;
  assert(native_gpu_submit(&owner, NARM_GPU_COPY_RECT, args, 14) == NARM_SERVICE_ERR_ARGUMENTS);
  args[2] = 1;
  assert(native_gpu_submit(&owner, NARM_GPU_COPY_RECT, args, 13) == NARM_SERVICE_ERR_ARGUMENTS);
  assert(native_gpu_submit(&owner, NARM_GPU_COPY_RECT, args, 14) == NARM_SERVICE_OK);
  assert(native_gpu_submit(&owner, NARM_GPU_BACK_TARGET, nullptr, 0) == 1);
  args[0] = 0;
  args[1] = 17;
  assert(native_gpu_submit(&owner, NARM_GPU_PRESENT, args, 2) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] = 1;
  assert(native_gpu_submit(&owner, NARM_GPU_PRESENT, args, 2) == NARM_SERVICE_OK);
  assert(last_token == 17 && native_gpu_submit(&owner, NARM_GPU_BACK_TARGET, nullptr, 0) == 0);

  args[0] = 0xfffffff0u;
  args[1] = 512u;
  args[2] = 0x1000u;
  assert(native_gpu_submit(&owner, NARM_GPU_READ_MEMORY, args, 3) == NARM_SERVICE_ERR_ARGUMENTS);
  args[0] = 0;
  assert(native_gpu_submit(&owner, NARM_GPU_READ_MEMORY, args, 3) == NARM_SERVICE_OK);
  assert(native_gpu_submit(&owner, 0xffffu, nullptr, 0) == NARM_SERVICE_ERR_UNKNOWN);

  args[0] = 0x12345678u;
  assert(native_gpu_submit(&owner, NARM_GPU_FENCE, args, 1) == NARM_SERVICE_OK);
  assert(last_token == args[0]);
  fail_fence = true;
  assert(native_gpu_submit(&owner, NARM_GPU_FENCE, args, 1) == NARM_SERVICE_ERR_DEVICE);
  assert(device_calls == 9);
  assert(native_gpu_submit(&owner, NARM_GPU_FRAME_READY, nullptr, 0) == 0);
  frame_result = NanoGpu::FrameReady;
  assert(native_gpu_submit(&owner, NARM_GPU_FRAME_READY, nullptr, 0) == 1);
  args[0] = 1; args[1] = 42;
  assert(native_gpu_submit(&owner, NARM_GPU_TRY_PRESENT, args, 2) == 1 && last_token == 42);
  frame_result = NanoGpu::FrameDeviceError;
  assert(native_gpu_submit(&owner, NARM_GPU_TRY_PRESENT, args, 2) == NARM_SERVICE_ERR_DEVICE);
}
