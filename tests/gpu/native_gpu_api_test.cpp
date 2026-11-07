#include "native_abi.h"
#include "native_live_diagnostics.h"
#include "nano_gpu.h"
#include <assert.h>

namespace {
NativeContextV1 owner{};
NanoGpu gpu;
bool focused = false;
bool fail_fence = false;
unsigned device_calls = 0;
uint32_t last_token = 0;

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

int main()
{
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
}
