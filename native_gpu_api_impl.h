#ifndef NATIVE_GPU_API_IMPL_H
#define NATIVE_GPU_API_IMPL_H

// This adapter is included after the native runtime's validation helpers.
bool native_gpu_range(uint32_t address, uint32_t bytes, uint32_t base)
{
  constexpr uint32_t memory_bytes = 0x800000u;
  return bytes != 0u && address >= base && address < memory_bytes &&
         bytes <= memory_bytes - address;
}

int32_t native_gpu_submit(NativeContextV1 *context, uint32_t command,
                          const uint32_t *args, uint32_t arg_words)
{
  if (!valid_service_arguments(context, args, arg_words))
    return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if (!has_capability(context, NARM_CAP_GPU))
    return NARM_SERVICE_ERR_CAPABILITY;
  if (!native_owns_gpu(context))
    return NARM_SERVICE_ERR_BUSY;

  native_fault_checkpoint(NARM_PHASE_VIDEO | command);
  NanoGpu &gpu = gpu_display_client();
  if (command == NARM_GPU_ENCODE_PASS || command == NARM_GPU_ENCODE_TILE ||
      command == NARM_GPU_ENCODE_PRIMITIVE || command == NARM_GPU_LINK_STATUS) {
    const uint32_t words = command == NARM_GPU_ENCODE_PASS ? 6u :
                           command == NARM_GPU_ENCODE_TILE ? 3u :
                           command == NARM_GPU_ENCODE_PRIMITIVE ? 2u : 1u;
    const uint32_t bytes = command == NARM_GPU_ENCODE_PASS ? 32u :
                           command == NARM_GPU_ENCODE_TILE ? 8u :
                           command == NARM_GPU_ENCODE_PRIMITIVE ? 320u : 64u;
    if (arg_words != words) return NARM_SERVICE_ERR_ARGUMENTS;
    auto *out = reinterpret_cast<uint8_t *>((uintptr_t)args[0]);
    if (!native_pointer_valid(context, out, bytes, 1u)) return NARM_SERVICE_ERR_ARGUMENTS;
    if (command == NARM_GPU_LINK_STATUS)
      return gpu_display_link_status(out) ? NARM_SERVICE_OK : NARM_SERVICE_ERR_DEVICE;
    bool encoded;
    if (command == NARM_GPU_ENCODE_PASS) {
      encoded = native_gpu_encode_pass(out, args[1], args[2], args[3], args[4], args[5]);
    } else if (command == NARM_GPU_ENCODE_TILE) {
      encoded = native_gpu_encode_tile(out, args[1], args[2]);
    } else {
      const auto *input = reinterpret_cast<const NativeGpuPrimitiveInput *>((uintptr_t)args[1]);
      if (!native_pointer_valid(context, input, sizeof(*input), alignof(NativeGpuPrimitiveInput)))
        return NARM_SERVICE_ERR_ARGUMENTS;
      encoded = native_gpu_encode_primitive(out, *input);
    }
    return encoded ? NARM_SERVICE_OK : NARM_SERVICE_ERR_ARGUMENTS;
  }
  if (command == NARM_GPU_ABORT || command == NARM_GPU_RESET) {
    if (arg_words != 0) return NARM_SERVICE_ERR_ARGUMENTS;
    const bool ok = command == NARM_GPU_ABORT ? gpu_display_abort() : gpu_display_begin();
    return ok ? NARM_SERVICE_OK : NARM_SERVICE_ERR_DEVICE;
  }
  if (command == NARM_GPU_FRAME_READY || command == NARM_GPU_TRY_PRESENT) {
    const bool try_present = command == NARM_GPU_TRY_PRESENT;
    if (arg_words != (try_present ? 2u : 0u) || (try_present && args[0] > 1u))
      return NARM_SERVICE_ERR_ARGUMENTS;
    const auto frame = try_present ? gpu.try_present(args[0], args[1]) : gpu.frame_ready();
    if (frame == NanoGpu::FrameDeviceError) return NARM_SERVICE_ERR_DEVICE;
    if (frame == NanoGpu::FrameArguments) return NARM_SERVICE_ERR_ARGUMENTS;
    return frame;
  }
  if (!gpu.ready()) return NARM_SERVICE_ERR_DEVICE;
  // An async present owns the reply stream until its completion is consumed.
  if (gpu.present_pending()) {
    const auto frame = gpu.frame_ready();
    if (frame == NanoGpu::FrameDeviceError) return NARM_SERVICE_ERR_DEVICE;
    if (frame != NanoGpu::FrameReady) return NARM_SERVICE_ERR_BUSY;
  }
  const auto result = [&gpu](bool success) -> int32_t {
    if (success) return NARM_SERVICE_OK;
    return gpu.ready() ? NARM_SERVICE_ERR_ARGUMENTS : NARM_SERVICE_ERR_DEVICE;
  };

  switch (command)
  {
    case NARM_GPU_GET_CAPS:
    case NARM_GPU_GET_STATUS:
    {
      if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
      auto *out = reinterpret_cast<uint8_t *>((uintptr_t)args[0]);
      if (!native_pointer_valid(context, out, 64u, 1u))
        return NARM_SERVICE_ERR_ARGUMENTS;
      return result(command == NARM_GPU_GET_CAPS ? gpu.get_caps(out) : gpu.get_status(out));
    }
    case NARM_GPU_WRITE_HEAP:
    {
      if (arg_words != 3u || args[2] > 956u ||
          !native_gpu_range(args[0], args[2], NanoGpu::kUserHeap))
        return NARM_SERVICE_ERR_ARGUMENTS;
      const auto *src = reinterpret_cast<const uint8_t *>((uintptr_t)args[1]);
      if (!native_pointer_valid(context, src, args[2], 1u))
        return NARM_SERVICE_ERR_ARGUMENTS;
      return result(gpu.write_heap(args[0], src, args[2]));
    }
    case NARM_GPU_SUBMIT_PASS:
      if (arg_words != 1u || (args[0] & 3u) != 0u ||
          !native_gpu_range(args[0], 32u, NanoGpu::kUserHeap))
        return NARM_SERVICE_ERR_ARGUMENTS;
      return result(gpu.submit_pass(args[0]));
    case NARM_GPU_COPY_RECT:
      if (arg_words != 14u || args[2] != gpu.back_target())
        return NARM_SERVICE_ERR_ARGUMENTS;
      return result(gpu.copy_rect(args));
    case NARM_GPU_FENCE:
      if (arg_words != 1u) return NARM_SERVICE_ERR_ARGUMENTS;
      return result(gpu.fence(args[0]));
    case NARM_GPU_PRESENT:
      if (arg_words != 2u || args[0] != gpu.back_target())
        return NARM_SERVICE_ERR_ARGUMENTS;
      return result(gpu.present(args[0], args[1]));
    case NARM_GPU_READ_MEMORY:
    {
      if (arg_words != 3u || args[1] > 512u ||
          !native_gpu_range(args[0], args[1], 0u))
        return NARM_SERVICE_ERR_ARGUMENTS;
      auto *out = reinterpret_cast<uint8_t *>((uintptr_t)args[2]);
      if (!native_pointer_valid(context, out, args[1], 1u))
        return NARM_SERVICE_ERR_ARGUMENTS;
      return result(gpu.read_memory(args[0], args[1], out));
    }
    case NARM_GPU_BACK_TARGET:
      if (arg_words != 0u) return NARM_SERVICE_ERR_ARGUMENTS;
      return (int32_t)gpu.back_target();
    default:
      return NARM_SERVICE_ERR_UNKNOWN;
  }
}

#endif
