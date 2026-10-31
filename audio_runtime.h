#ifndef AUDIO_RUNTIME_H
#define AUDIO_RUNTIME_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

static constexpr uint8_t AUDIO_VOICE_CHANNELS = 8;
static constexpr uint32_t AUDIO_OUTPUT_SAMPLE_RATE = 48000;

// Call on Core 0 after VGA has claimed and started its PIO/DMA resources.
bool audio_runtime_init();
bool audio_runtime_ready();
bool audio_output_ready();

// Core 1 exclusively owns the I2S object and mixer.
bool audio_core1_begin();
void audio_core1_poll();

// Core 0 services fragmented-file fallback streams. Contiguous WAV files are
// read by the Core-1 SPI-DMA engine; all filesystem access is bus-arbitrated.
void audio_core0_poll();
// Cheap cooperative hook for long-running Core-0 execution paths. It performs
// a real refill at most once per service interval.
void audio_core0_poll_if_due();

// Public playback controls. Call these from Core 0 (the NARM/terminal core).
// WAV support: uncompressed PCM, mono or stereo, 8/16/24/32 bits.
bool play_sound_SD(uint8_t voice_channel, const String& fileName);
// Starts at a zero-based decoded PCM frame within the WAV data chunk.
// start_frame must be smaller than the WAV's total frame count.
bool play_sound_SD_from_frame(uint8_t voice_channel, const String& fileName,
                              uint32_t start_frame);
bool stop_voice_channel(uint8_t voice_channel);
void stop_all_voice_channels();

// Arrays contain signed 16-bit interleaved PCM and remain owned by the caller.
// They must stay valid until the voice finishes or is stopped.
bool play_sound_array(uint8_t voice_channel,
                      const int16_t* samples,
                      uint32_t frame_count,
                      uint32_t sample_rate,
                      uint8_t channel_count = 1);
// frame_count describes the complete array. Playback begins at start_frame
// and continues through frame_count - 1 without copying the samples.
bool play_sound_array_from_frame(uint8_t voice_channel,
                                 const int16_t* samples,
                                 uint32_t frame_count,
                                 uint32_t start_frame,
                                 uint32_t sample_rate,
                                 uint8_t channel_count = 1);

// Convenient overloads for real C/C++ arrays: their length is inferred safely.
template <size_t N>
bool play_sound_array(uint8_t voice_channel,
                      const int16_t (&samples)[N],
                      uint32_t sample_rate,
                      uint8_t channel_count = 1)
{
  if(channel_count == 0 || (N % channel_count) != 0)
  {
    return false;
  }

  return play_sound_array(voice_channel,
                          samples,
                          (uint32_t)(N / channel_count),
                          sample_rate,
                          channel_count);
}

template <size_t N>
bool play_sound_array(const int16_t (&samples)[N], uint32_t sample_rate)
{
  return play_sound_array(0, samples, (uint32_t)N, sample_rate, 1);
}

bool voice_channel_active(uint8_t voice_channel);
// Pausing preserves the voice's decode/mix position. This is used when a
// foreground-only NARM process loses focus.
bool audio_set_voice_paused(uint8_t voice_channel, bool paused);

struct AudioVoicePosition
{
  uint32_t current_frame;
  uint32_t total_frames;
  uint32_t sample_rate;
  bool active;
  bool paused;
};

// Coherent snapshot of the source frame mixed by Core 1, including the seek
// offset. The I2S output queue introduces up to roughly 64 ms of audio latency.
// An inactive voice retains the last completed track's position until reset.
bool audio_get_voice_position(uint8_t voice_channel, AudioVoicePosition &position);
uint32_t audio_output_underflow_count();
uint32_t audio_stream_starvation_count();

// True after Core 1 has consumed every queued playback command. Used by the
// NARM lifecycle to ensure caller-owned PCM is no longer referenced at exit.
bool audio_command_queue_idle();

#endif
