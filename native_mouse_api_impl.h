#ifndef NATIVE_MOUSE_API_IMPL_H
#define NATIVE_MOUSE_API_IMPL_H

// Included inside native_runtime.cpp's namespace after its context/pointer
// validators. The host service-boundary tests include this same implementation.
int32_t native_mouse_check(NativeContextV1 *context)
{
  if(!valid_native_context(context)) return NARM_SERVICE_ERR_ARGUMENTS;
  abort_if_stop_requested(context);
  if(!has_capability(context, NARM_CAP_MOUSE)) return NARM_SERVICE_ERR_CAPABILITY;
  return NARM_SERVICE_OK;
}

int32_t native_mouse_get_state(NativeContextV1 *context, NativeMouseStateV1 *state)
{
  if(!native_pointer_valid(context, state, sizeof(*state))) return NARM_SERVICE_ERR_ARGUMENTS;
  const int32_t status = native_mouse_check(context);
  if(status != NARM_SERVICE_OK) return status;
  const NativeProcess *process = (NativeProcess *)context->process_handle;
  mouse_input_get_state(process->pid, *state);
  return NARM_SERVICE_OK;
}

int32_t native_mouse_poll_event(NativeContextV1 *context, NativeMouseEventV1 *event)
{
  if(!native_pointer_valid(context, event, sizeof(*event))) return NARM_SERVICE_ERR_ARGUMENTS;
  const int32_t status = native_mouse_check(context);
  if(status != NARM_SERVICE_OK) return status;
  const NativeProcess *process = (NativeProcess *)context->process_handle;
  return mouse_input_poll_event(process->pid, *event) ? 1 : 0;
}

int32_t native_mouse_read_motion(NativeContextV1 *context, NativeMouseMotionV1 *motion)
{
  if(!native_pointer_valid(context, motion, sizeof(*motion))) return NARM_SERVICE_ERR_ARGUMENTS;
  const int32_t status = native_mouse_check(context);
  if(status != NARM_SERVICE_OK) return status;
  const NativeProcess *process = (NativeProcess *)context->process_handle;
  mouse_input_read_motion(process->pid, *motion);
  return NARM_SERVICE_OK;
}

int32_t native_mouse_set_cursor_visible(NativeContextV1 *context, uint32_t visible)
{
  if(visible > 1u) return NARM_SERVICE_ERR_ARGUMENTS;
  const int32_t status = native_mouse_check(context);
  if(status != NARM_SERVICE_OK) return status;
  const NativeProcess *process = (NativeProcess *)context->process_handle;
  return mouse_input_set_cursor_visible(process->pid, visible != 0u)
             ? NARM_SERVICE_OK : NARM_SERVICE_ERR_BUSY;
}

int32_t native_mouse_set_relative_mode(NativeContextV1 *context, uint32_t enabled)
{
  if(enabled > 1u) return NARM_SERVICE_ERR_ARGUMENTS;
  const int32_t status = native_mouse_check(context);
  if(status != NARM_SERVICE_OK) return status;
  const NativeProcess *process = (NativeProcess *)context->process_handle;
  return mouse_input_set_relative_mode(process->pid, enabled != 0u)
             ? NARM_SERVICE_OK : NARM_SERVICE_ERR_BUSY;
}

uint32_t native_mouse_dropped_events(NativeContextV1 *context)
{
  if(native_mouse_check(context) != NARM_SERVICE_OK) return 0;
  const NativeProcess *process = (NativeProcess *)context->process_handle;
  return mouse_input_dropped_events(process->pid);
}

const NativeMouseApiV1 native_mouse_api = {
  sizeof(NativeMouseApiV1), 1, 0,
  native_mouse_get_state, native_mouse_poll_event, native_mouse_read_motion,
  native_mouse_set_cursor_visible, native_mouse_set_relative_mode,
  native_mouse_dropped_events
};

#endif
