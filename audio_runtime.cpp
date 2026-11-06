#include "audio_runtime.h"

#include <I2S.h>
#include <SPI.h>
#include <SdFat.h>
#include <hardware/dma.h>
#include <hardware/spi.h>
#include <pico/platform.h>
#include <string.h>

#include "SDmgr.h"
#include "globals.h"

namespace
{
constexpr pin_size_t kBclkPin = 6;
constexpr pin_size_t kDataPin = 8;

// These are the I2S library's internal-SRAM DMA buffers.  The mixer shares
// Core 1 with the keyboard and optional 3D worker, so keep enough queued audio
// to survive a moderately long worker pass without falling back to silence.
// 12 * 256 stereo frames is 64 ms at 48 kHz and costs about 12 KiB.
constexpr uint16_t kI2sBufferCount = 12;
constexpr uint16_t kI2sBufferWords = 256;
// AudioBufferManager allocates one extra silence buffer and does not check
// each allocation. Leave room for its nodes, PIO object and normal firmware
// heap use before entering that constructor on the keyboard's Core 1.
constexpr uint32_t kI2sMinimumStartupHeap =
  (uint32_t)(kI2sBufferCount + 1u) * kI2sBufferWords * sizeof(uint32_t) + 8192u;
static uint32_t g_nextI2sHeapRetryUs = 0;
constexpr uint16_t kMixBlockFrames = 128;
constexpr uint8_t kMaxMixBlocksPerPoll = 24;

// Large contiguous WAVs are refilled by SPI DMA on Core 1. Fragmented files
// use the cooperative Core-0 fallback, so retain generous ring headroom for
// either path. Keep all 32 rings within the 1 MiB audio budget on the 8 MiB board.
constexpr uint32_t kSdRingFrames = 8192;
constexpr uint32_t kSdRingMask = kSdRingFrames - 1;
constexpr uint32_t kStartPrefillFrames = kSdRingFrames / 2;
constexpr uint16_t kSdReadFrames = 4096;
constexpr uint16_t kSdMinimumRefillFrames = 4096;
constexpr uint32_t kCore0ServiceIntervalUs = 1000;
constexpr uint8_t kMaximumWaveBlockAlign = 8;
constexpr uint8_t kCommandQueueLength = 32;
constexpr uint16_t kSdSectorBytes = 512;
constexpr uint8_t kSdDmaBurstSectors = 32;
constexpr uint32_t kSdTokenTimeoutMs = 300;

static_assert((kSdRingFrames & (kSdRingFrames - 1)) == 0,
              "The SD audio ring size must be a power of two");

static_assert(AUDIO_VOICE_CHANNELS <= 32, "Audio voice masks must fit in 32 bits");

struct StereoFrame
{
  int16_t left;
  int16_t right;
};

struct MixAccumulator
{
  int32_t left;
  int32_t right;
};

enum class AudioCommandType : uint8_t
{
  PlaySd,
  PlayArray,
  Stop
};

struct AudioCommand
{
  AudioCommandType type;
  uint8_t voiceChannel;
  uint8_t channelCount;
  uint8_t reserved;
  uint32_t generation;
  uint32_t sampleRate;
  uint32_t frameCount;
  uint32_t startFrame;
  uint32_t totalFrames;
  const int16_t* arraySamples;
  uint32_t sequence;
};

struct StreamRingControl
{
  volatile uint32_t generation;
  volatile uint32_t writeIndex;
  volatile uint32_t readIndex;
  volatile uint32_t eof;
};

struct SdStream
{
  FirmwareFile file;
  bool active;
  bool dmaStreaming;
  uint8_t channels;
  uint8_t bitsPerSample;
  uint16_t blockAlign;
  uint32_t sampleRate;
  uint32_t framesRemaining;
  uint32_t generation;
  Sector_t dmaNextSector;
  Sector_t dmaLastSector;
  uint32_t dmaBytesRemaining;
  uint16_t dmaSectorOffset;
  uint8_t partialFrameBytes;
  uint8_t partialFrame[kMaximumWaveBlockAlign];
};

enum class SdDmaState : uint8_t
{
  Idle,
  WaitToken,
  Transfer,
  TransferReady
};

struct SdDmaController
{
  SdDmaState state;
  uint8_t voiceChannel;
  uint8_t bufferIndex;
  uint8_t sectorsRemaining;
  bool cardReadStarted;
  bool busHeld;
  uint32_t tokenDeadlineMs;
};

enum class VoiceSource : uint8_t
{
  None,
  Sd,
  Array
};

enum class SourceReadResult : uint8_t
{
  Available,
  Waiting,
  End
};

struct MixerVoice
{
  bool active;
  bool haveSample0;
  bool haveSample1;
  bool endAfterSample1;
  bool starvationReported;
  bool ringBlockActive;
  bool ringBlockEof;
  VoiceSource source;
  uint8_t channelCount;
  uint32_t generation;
  uint32_t sampleRate;
  uint32_t sourceCursor;
  uint32_t frameCount;
  uint32_t playbackFrame;
  uint32_t totalFrames;
  const int16_t* arraySamples;
  uint32_t phaseQ16;
  uint32_t phaseStepQ16;
  int16_t gainLeftQ15;
  int16_t gainRightQ15;
  StereoFrame sample0;
  StereoFrame sample1;
  uint32_t ringBlockGeneration;
  uint32_t ringBlockReadIndex;
  uint32_t ringBlockWriteIndex;
};

I2S g_pcm5102(OUTPUT);

volatile uint32_t g_runtimeReady = 0;
volatile uint32_t g_core1Started = 0;
volatile uint32_t g_activeVoiceMask = 0;
volatile uint32_t g_pausedVoiceMask = 0;
volatile uint32_t g_dmaStreamMask = 0;
volatile uint32_t g_outputUnderflows = 0;
volatile uint32_t g_streamStarvations = 0;

// Keep frequently touched control/mixer state in internal SRAM.  The large
// sample rings below are the only audio objects that need PSRAM capacity.
SdStream g_sdStreams[AUDIO_VOICE_CHANNELS];
StreamRingControl g_ringControl[AUDIO_VOICE_CHANNELS];
MixerVoice g_voices[AUDIO_VOICE_CHANNELS];

// Single writer (Core 1), lock-free readers (Core 0). Only publish once per
// mixer block so sample rendering performs no cross-core atomic operations.
struct VoicePositionSnapshot
{
  volatile uint32_t sequence;
  volatile uint32_t currentFrame;
  volatile uint32_t totalFrames;
  volatile uint32_t sampleRate;
  volatile uint32_t active;
};
VoicePositionSnapshot g_voicePositions[AUDIO_VOICE_CHANNELS]{};

// Exactly one producer (Core 0) and one consumer (Core 1) use this queue.
AudioCommand g_commandQueue[kCommandQueueLength];
volatile uint32_t g_commandWriteIndex = 0;
volatile uint32_t g_commandReadIndex = 0;
// Dequeuing releases a queue slot before the command has changed the mixer.
// A separate acknowledgement is published only after the new snapshot exists.
volatile uint32_t g_voiceCommandEnqueued[AUDIO_VOICE_CHANNELS]{};
volatile uint32_t g_voiceCommandApplied[AUDIO_VOICE_CHANNELS]{};

// PCM rings live in PSRAM. DMA never accesses them directly: Core 1 mixes from
// these rings into the I2S library's internal SRAM buffers.
PSRAM StereoFrame g_sdPcm[AUDIO_VOICE_CHANNELS][kSdRingFrames];
static_assert(sizeof(g_sdPcm) <= 1024u * 1024u, "SD audio exceeds its PSRAM budget");

alignas(4) uint32_t g_mixBlock[kMixBlockFrames];
alignas(4) MixAccumulator g_mixAccumulator[kMixBlockFrames];
size_t g_pendingByteOffset = 0;
size_t g_pendingByteCount = 0;

// SdFat and the decoder touch this linearly during every refill.  Internal
// SRAM avoids making the SPI read and Core-1 mixer compete for PSRAM cycles.
alignas(4) uint8_t g_sdReadScratch[kSdReadFrames * kMaximumWaveBlockAlign];
alignas(4) uint8_t g_sdDmaBuffers[2][kSdSectorBytes];
uint8_t g_sdRoundRobinCursor = 0;
uint8_t g_sdDmaRoundRobinCursor = 0;
uint32_t g_generationCounter = 1;
uint32_t g_nextCore0ServiceUs = 0;
SdDmaController g_sdDma{};
int g_spiRxDmaChannel = -1;
int g_spiTxDmaChannel = -1;
alignas(4) uint32_t g_spiDmaDummy = 0xffffffffu;

static inline uint32_t atomicLoadAcquire(const volatile uint32_t* value)
{
  return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline uint32_t atomicLoadRelaxed(const volatile uint32_t* value)
{
  return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static inline void atomicStoreRelease(volatile uint32_t* value, uint32_t next)
{
  __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static bool initializeAudioSpiDma()
{
  if(g_spiRxDmaChannel >= 0 && g_spiTxDmaChannel >= 0)
  {
    return true;
  }

  const int rxChannel = dma_claim_unused_channel(false);
  if(rxChannel < 0)
  {
    return false;
  }

  const int txChannel = dma_claim_unused_channel(false);
  if(txChannel < 0)
  {
    dma_channel_unclaim(rxChannel);
    return false;
  }

  g_spiRxDmaChannel = rxChannel;
  g_spiTxDmaChannel = txChannel;

  // These channels are completion-polled. Explicitly keep them off both DMA
  // IRQ lines even if a previously released channel retained old IRQ state.
  dma_channel_set_irq0_enabled(g_spiRxDmaChannel, false);
  dma_channel_set_irq0_enabled(g_spiTxDmaChannel, false);
  dma_channel_set_irq1_enabled(g_spiRxDmaChannel, false);
  dma_channel_set_irq1_enabled(g_spiTxDmaChannel, false);
  dma_channel_acknowledge_irq0(g_spiRxDmaChannel);
  dma_channel_acknowledge_irq0(g_spiTxDmaChannel);
  dma_channel_acknowledge_irq1(g_spiRxDmaChannel);
  dma_channel_acknowledge_irq1(g_spiTxDmaChannel);
  return true;
}

static uint16_t readLe16(const uint8_t* bytes)
{
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t readLe32(const uint8_t* bytes)
{
  return (uint32_t)bytes[0]
       | ((uint32_t)bytes[1] << 8)
       | ((uint32_t)bytes[2] << 16)
       | ((uint32_t)bytes[3] << 24);
}

static bool readExact(FirmwareFile& file, void* destination, size_t bytes)
{
  return file.read(destination, bytes) == (int)bytes;
}

struct WaveInfo
{
  uint8_t channels;
  uint8_t bitsPerSample;
  uint16_t blockAlign;
  uint32_t sampleRate;
  uint32_t dataOffset;
  uint32_t dataBytes;
};

static bool parseWaveFile(FirmwareFile& file, WaveInfo& info)
{
  uint8_t riffHeader[12];
  if(!readExact(file, riffHeader, sizeof(riffHeader)) ||
     memcmp(riffHeader, "RIFF", 4) != 0 ||
     memcmp(riffHeader + 8, "WAVE", 4) != 0)
  {
    return false;
  }

  bool haveFormat = false;
  bool haveData = false;
  uint16_t formatTag = 0;
  uint32_t dataOffset = 0;
  uint32_t dataBytes = 0;

  for(uint16_t chunkIndex = 0; chunkIndex < 128; chunkIndex++)
  {
    uint8_t chunkHeader[8];
    if(!readExact(file, chunkHeader, sizeof(chunkHeader)))
    {
      break;
    }

    const uint32_t chunkBytes = readLe32(chunkHeader + 4);
    const uint32_t chunkStart = (uint32_t)file.curPosition();
    const uint64_t nextChunk64 = (uint64_t)chunkStart + chunkBytes + (chunkBytes & 1u);
    if(nextChunk64 > 0xffffffffULL || nextChunk64 > file.fileSize())
    {
      return false;
    }

    if(memcmp(chunkHeader, "fmt ", 4) == 0)
    {
      if(chunkBytes < 16)
      {
        return false;
      }

      uint8_t format[16];
      if(!readExact(file, format, sizeof(format)))
      {
        return false;
      }

      formatTag = readLe16(format + 0);
      info.channels = (uint8_t)readLe16(format + 2);
      info.sampleRate = readLe32(format + 4);
      info.blockAlign = readLe16(format + 12);
      info.bitsPerSample = (uint8_t)readLe16(format + 14);
      haveFormat = true;
    }
    else if(memcmp(chunkHeader, "data", 4) == 0)
    {
      dataOffset = chunkStart;
      dataBytes = chunkBytes;
      haveData = true;
    }

    if(haveFormat && haveData)
    {
      break;
    }

    if(!file.seekSet((uint32_t)nextChunk64))
    {
      return false;
    }
  }

  const uint8_t bytesPerSample = (uint8_t)((info.bitsPerSample + 7u) / 8u);
  if(!haveFormat || !haveData || formatTag != 1 ||
     (info.channels != 1 && info.channels != 2) ||
     (info.bitsPerSample != 8 && info.bitsPerSample != 16 &&
      info.bitsPerSample != 24 && info.bitsPerSample != 32) ||
     info.sampleRate < 8000 || info.sampleRate > 192000 ||
     info.blockAlign < (uint16_t)(info.channels * bytesPerSample) ||
     info.blockAlign > kMaximumWaveBlockAlign || dataBytes < info.blockAlign)
  {
    return false;
  }

  info.dataOffset = dataOffset;
  info.dataBytes = dataBytes;
  return file.seekSet(dataOffset);
}

static int16_t decodePcmSample(const uint8_t* source, uint8_t bitsPerSample)
{
  if(bitsPerSample == 8)
  {
    return (int16_t)(((int32_t)source[0] - 128) << 8);
  }

  if(bitsPerSample == 16)
  {
    return (int16_t)readLe16(source);
  }

  if(bitsPerSample == 24)
  {
    int32_t value = (int32_t)source[0]
                  | ((int32_t)source[1] << 8)
                  | ((int32_t)source[2] << 16);
    if((value & 0x00800000) != 0)
    {
      value |= (int32_t)0xff000000;
    }
    return (int16_t)(value >> 8);
  }

  const int32_t value = (int32_t)readLe32(source);
  return (int16_t)(value >> 16);
}

static uint32_t nextGeneration()
{
  g_generationCounter++;
  if(g_generationCounter == 0)
  {
    g_generationCounter = 1;
  }
  return g_generationCounter;
}

static void closeSdStream(uint8_t voiceChannel)
{
  SdStream& stream = g_sdStreams[voiceChannel];
  if(stream.file)
  {
    stream.file.close();
  }
  stream.active = false;
  stream.dmaStreaming = false;
  stream.framesRemaining = 0;
  stream.dmaBytesRemaining = 0;
  stream.partialFrameBytes = 0;
  __atomic_fetch_and(&g_dmaStreamMask,
                     ~(1u << voiceChannel),
                     __ATOMIC_RELEASE);
}

static void resetStreamRing(uint8_t voiceChannel, uint32_t generation, bool eof)
{
  StreamRingControl& ring = g_ringControl[voiceChannel];

  // Publish the new generation first. A consumer that was in the middle of an
  // old read rechecks the generation before committing its read index.
  atomicStoreRelease(&ring.generation, generation);
  atomicStoreRelease(&ring.readIndex, 0);
  atomicStoreRelease(&ring.writeIndex, 0);
  atomicStoreRelease(&ring.eof, eof ? 1u : 0u);
}

static uint32_t invalidateSdStream(uint8_t voiceChannel)
{
  closeSdStream(voiceChannel);
  const uint32_t generation = nextGeneration();
  g_sdStreams[voiceChannel].generation = generation;
  resetStreamRing(voiceChannel, generation, true);
  return generation;
}

static void finishSdStream(uint8_t voiceChannel)
{
  closeSdStream(voiceChannel);
  atomicStoreRelease(&g_ringControl[voiceChannel].eof, 1u);
}

static bool fillSdStreamChunk(uint8_t voiceChannel)
{
  SdStream& stream = g_sdStreams[voiceChannel];
  StreamRingControl& ring = g_ringControl[voiceChannel];

  if(stream.dmaStreaming)
  {
    return false;
  }

  if(!stream.active || stream.framesRemaining == 0)
  {
    if(stream.active)
    {
      finishSdStream(voiceChannel);
    }
    return false;
  }

  const uint32_t writeIndex = atomicLoadRelaxed(&ring.writeIndex);
  const uint32_t readIndex = atomicLoadAcquire(&ring.readIndex);
  const uint32_t usedFrames = writeIndex - readIndex;
  if(usedFrames >= kSdRingFrames)
  {
    return false;
  }

  const uint32_t freeFrames = kSdRingFrames - usedFrames;

  // Avoid issuing tiny SD transactions merely because Core 1 consumed a few
  // frames since the last poll.  Always allow the final short read at EOF.
  if(freeFrames < kSdMinimumRefillFrames && stream.framesRemaining > freeFrames)
  {
    return false;
  }

  uint32_t framesToRead = freeFrames;
  if(framesToRead > kSdReadFrames)
  {
    framesToRead = kSdReadFrames;
  }
  if(framesToRead > stream.framesRemaining)
  {
    framesToRead = stream.framesRemaining;
  }

  const size_t bytesToRead = (size_t)framesToRead * stream.blockAlign;
  const int bytesRead = stream.file.read(g_sdReadScratch, bytesToRead);
  if(bytesRead <= 0)
  {
    finishSdStream(voiceChannel);
    return false;
  }

  const uint32_t framesRead = (uint32_t)bytesRead / stream.blockAlign;
  const uint8_t bytesPerSample = (uint8_t)((stream.bitsPerSample + 7u) / 8u);

  for(uint32_t frame = 0; frame < framesRead; frame++)
  {
    const uint8_t* source = g_sdReadScratch + ((size_t)frame * stream.blockAlign);
    StereoFrame decoded;
    decoded.left = decodePcmSample(source, stream.bitsPerSample);
    decoded.right = stream.channels == 2
                  ? decodePcmSample(source + bytesPerSample, stream.bitsPerSample)
                  : decoded.left;
    g_sdPcm[voiceChannel][(writeIndex + frame) & kSdRingMask] = decoded;
  }

  if(framesRead != 0)
  {
    atomicStoreRelease(&ring.writeIndex, writeIndex + framesRead);
    stream.framesRemaining -= framesRead;
  }

  if(framesRead < framesToRead || stream.framesRemaining == 0)
  {
    finishSdStream(voiceChannel);
  }

  return framesRead != 0;
}

static SdSpiCard* audioSpiCard()
{
  SdCard* card = SD.card();
  if(card == nullptr || !card->isSpi())
  {
    return nullptr;
  }
  return static_cast<SdSpiCard*>(card);
}

static bool endSdDmaBurst()
{
  bool stopped = true;
  if(g_sdDma.cardReadStarted)
  {
    SdSpiCard* card = audioSpiCard();
    stopped = card != nullptr && card->readStop();
  }

  g_sdDma.state = SdDmaState::Idle;
  g_sdDma.cardReadStarted = false;
  g_sdDma.sectorsRemaining = 0;

  if(g_sdDma.busHeld)
  {
    g_sdDma.busHeld = false;
    sdBusRelease();
  }
  return stopped;
}

static void failSdDmaBurst()
{
  const uint8_t voiceChannel = g_sdDma.voiceChannel;
  endSdDmaBurst();
  if(voiceChannel < AUDIO_VOICE_CHANNELS &&
     g_sdStreams[voiceChannel].dmaStreaming)
  {
    finishSdStream(voiceChannel);
  }
}

static bool consumeSdDmaSector(uint8_t voiceChannel, const uint8_t* sector)
{
  SdStream& stream = g_sdStreams[voiceChannel];
  StreamRingControl& ring = g_ringControl[voiceChannel];
  if(!stream.active || !stream.dmaStreaming || stream.framesRemaining == 0 ||
     stream.dmaBytesRemaining == 0)
  {
    return true;
  }

  const uint16_t sectorOffset = stream.dmaSectorOffset;
  uint32_t byteCount = kSdSectorBytes - sectorOffset;
  if(byteCount > stream.dmaBytesRemaining)
  {
    byteCount = stream.dmaBytesRemaining;
  }

  const uint32_t writeIndex = atomicLoadRelaxed(&ring.writeIndex);
  const uint32_t readIndex = atomicLoadAcquire(&ring.readIndex);
  const uint32_t freeFrames = kSdRingFrames - (writeIndex - readIndex);
  uint32_t producedFrames = 0;
  const uint8_t* source = sector + sectorOffset;
  const uint8_t bytesPerSample =
    (uint8_t)((stream.bitsPerSample + 7u) / 8u);

  for(uint32_t byte = 0; byte < byteCount; byte++)
  {
    stream.partialFrame[stream.partialFrameBytes++] = source[byte];
    if(stream.partialFrameBytes != stream.blockAlign)
    {
      continue;
    }

    if(producedFrames >= freeFrames)
    {
      return false;
    }

    StereoFrame decoded;
    decoded.left = decodePcmSample(stream.partialFrame, stream.bitsPerSample);
    decoded.right = stream.channels == 2
                  ? decodePcmSample(stream.partialFrame + bytesPerSample,
                                    stream.bitsPerSample)
                  : decoded.left;
    g_sdPcm[voiceChannel][(writeIndex + producedFrames) & kSdRingMask] = decoded;
    producedFrames++;
    stream.partialFrameBytes = 0;
    stream.framesRemaining--;
  }

  if(producedFrames != 0)
  {
    atomicStoreRelease(&ring.writeIndex, writeIndex + producedFrames);
  }

  stream.dmaBytesRemaining -= byteCount;
  stream.dmaSectorOffset = 0;
  stream.dmaNextSector++;

  return stream.framesRemaining == 0 || stream.dmaBytesRemaining == 0;
}

static bool startSdDmaSectorTransfer()
{
  uint8_t* destination = g_sdDmaBuffers[g_sdDma.bufferIndex];
  if(g_spiRxDmaChannel >= 0 && g_spiTxDmaChannel >= 0)
  {
    spi_hw_t* spiHardware = spi_get_hw(spi0);

    dma_channel_config rxConfig =
      dma_channel_get_default_config(g_spiRxDmaChannel);
    channel_config_set_transfer_data_size(&rxConfig, DMA_SIZE_8);
    channel_config_set_read_increment(&rxConfig, false);
    channel_config_set_write_increment(&rxConfig, true);
    channel_config_set_dreq(&rxConfig, spi_get_dreq(spi0, false));
    channel_config_set_irq_quiet(&rxConfig, true);
    dma_channel_configure(g_spiRxDmaChannel,
                          &rxConfig,
                          destination,
                          &spiHardware->dr,
                          kSdSectorBytes,
                          false);

    dma_channel_config txConfig =
      dma_channel_get_default_config(g_spiTxDmaChannel);
    channel_config_set_transfer_data_size(&txConfig, DMA_SIZE_8);
    channel_config_set_read_increment(&txConfig, false);
    channel_config_set_write_increment(&txConfig, false);
    channel_config_set_dreq(&txConfig, spi_get_dreq(spi0, true));
    channel_config_set_irq_quiet(&txConfig, true);
    dma_channel_configure(g_spiTxDmaChannel,
                          &txConfig,
                          &spiHardware->dr,
                          &g_spiDmaDummy,
                          kSdSectorBytes,
                          false);

    // RX must be listening before TX clocks the first byte. Completion is
    // polled from Core 1, avoiding an IRQ and two channel allocations for
    // every 512-byte sector.
    spiHardware->dmacr = 1u | (1u << 1);
    dma_start_channel_mask((1u << g_spiRxDmaChannel)
                         | (1u << g_spiTxDmaChannel));
    g_sdDma.state = SdDmaState::Transfer;
    return true;
  }

  // If the permanent channels could not be reserved at startup, preserve
  // correct playback with a synchronous Core-1 transfer.
  SPI.transfer((const void*)nullptr, destination, kSdSectorBytes);
  g_sdDma.state = SdDmaState::TransferReady;
  return true;
}

static bool startSdDmaBurstForVoice(uint8_t voiceChannel)
{
  if(!sdBusTryAcquire())
  {
    return false;
  }
  g_sdDma.busHeld = true;

  SdStream& stream = g_sdStreams[voiceChannel];
  StreamRingControl& ring = g_ringControl[voiceChannel];
  if(!stream.active || !stream.dmaStreaming || stream.framesRemaining == 0 ||
     stream.dmaBytesRemaining == 0)
  {
    endSdDmaBurst();
    return false;
  }

  const uint32_t writeIndex = atomicLoadRelaxed(&ring.writeIndex);
  const uint32_t readIndex = atomicLoadAcquire(&ring.readIndex);
  const uint32_t freeFrames = kSdRingFrames - (writeIndex - readIndex);
  if(freeFrames < kSdMinimumRefillFrames && stream.framesRemaining > freeFrames)
  {
    endSdDmaBurst();
    return false;
  }

  const uint32_t capacityBytes = freeFrames * stream.blockAlign;
  uint32_t plannedBytes = 0;
  uint32_t remainingBytes = stream.dmaBytesRemaining;
  uint16_t offset = stream.dmaSectorOffset;
  Sector_t sector = stream.dmaNextSector;
  uint8_t sectors = 0;

  while(sectors < kSdDmaBurstSectors && sector <= stream.dmaLastSector &&
        remainingBytes != 0)
  {
    uint32_t bytesInSector = kSdSectorBytes - offset;
    if(bytesInSector > remainingBytes)
    {
      bytesInSector = remainingBytes;
    }
    if(plannedBytes + bytesInSector > capacityBytes)
    {
      break;
    }
    plannedBytes += bytesInSector;
    remainingBytes -= bytesInSector;
    offset = 0;
    sector++;
    sectors++;
  }

  SdSpiCard* card = audioSpiCard();
  if(sectors == 0 || card == nullptr || !card->syncDevice() ||
     !card->readStart(stream.dmaNextSector))
  {
    endSdDmaBurst();
    return false;
  }

  g_sdDma.voiceChannel = voiceChannel;
  g_sdDma.bufferIndex = 0;
  g_sdDma.sectorsRemaining = sectors;
  g_sdDma.cardReadStarted = true;
  g_sdDma.tokenDeadlineMs = millis() + kSdTokenTimeoutMs;
  g_sdDma.state = SdDmaState::WaitToken;
  return true;
}

static void serviceSdDma()
{
  if(g_sdDma.state == SdDmaState::Idle)
  {
    const uint32_t streamMask = atomicLoadAcquire(&g_dmaStreamMask);
    if(streamMask == 0) return;

    for(uint8_t checked = 0; checked < AUDIO_VOICE_CHANNELS; checked++)
    {
      const uint8_t voiceChannel = g_sdDmaRoundRobinCursor;
      g_sdDmaRoundRobinCursor = (uint8_t)((g_sdDmaRoundRobinCursor + 1)
                                        % AUDIO_VOICE_CHANNELS);
      if((streamMask & (1u << voiceChannel)) != 0 &&
         startSdDmaBurstForVoice(voiceChannel))
      {
        break;
      }
    }
    return;
  }

  if(g_sdDma.state == SdDmaState::WaitToken)
  {
    // Poll only a few bytes per pass so a slow/erroring card cannot hold Core 1
    // away from the keyboard and I2S mixer for the full SD timeout.
    for(uint8_t attempt = 0; attempt < 8; attempt++)
    {
      const uint8_t token = SPI.transfer(0xff);
      if(token == DATA_START_SECTOR)
      {
        startSdDmaSectorTransfer();
        return;
      }
      if(token != 0xff)
      {
        failSdDmaBurst();
        return;
      }
    }
    if((int32_t)(millis() - g_sdDma.tokenDeadlineMs) >= 0)
    {
      failSdDmaBurst();
    }
    return;
  }

  if(g_sdDma.state == SdDmaState::Transfer)
  {
    if(dma_channel_is_busy(g_spiRxDmaChannel) ||
       dma_channel_is_busy(g_spiTxDmaChannel) || spi_is_busy(spi0))
    {
      return;
    }

    spi_get_hw(spi0)->dmacr = 0;
    g_sdDma.state = SdDmaState::TransferReady;
  }

  // The payload is now in SRAM. Discard the two SPI-mode CRC bytes before the
  // next token or CMD12, just as SdFat's synchronous readData() does.
  SPI.transfer(0xff);
  SPI.transfer(0xff);

  const uint8_t voiceChannel = g_sdDma.voiceChannel;
  if(voiceChannel >= AUDIO_VOICE_CHANNELS)
  {
    failSdDmaBurst();
    return;
  }

  const bool streamFinished =
    consumeSdDmaSector(voiceChannel, g_sdDmaBuffers[g_sdDma.bufferIndex]);
  if(g_sdDma.sectorsRemaining != 0)
  {
    g_sdDma.sectorsRemaining--;
  }

  if(streamFinished || g_sdDma.sectorsRemaining == 0)
  {
    const bool stopped = endSdDmaBurst();
    if(streamFinished || !stopped)
    {
      finishSdStream(voiceChannel);
    }
    return;
  }

  g_sdDma.bufferIndex ^= 1u;
  g_sdDma.tokenDeadlineMs = millis() + kSdTokenTimeoutMs;
  g_sdDma.state = SdDmaState::WaitToken;
}

static bool enqueueCommand(const AudioCommand& command)
{
  const uint32_t writeIndex = atomicLoadRelaxed(&g_commandWriteIndex);
  const uint32_t readIndex = atomicLoadAcquire(&g_commandReadIndex);
  if(writeIndex - readIndex >= kCommandQueueLength)
  {
    return false;
  }

  AudioCommand& queued = g_commandQueue[writeIndex % kCommandQueueLength];
  queued = command;
  volatile uint32_t* enqueued = &g_voiceCommandEnqueued[command.voiceChannel];
  queued.sequence = atomicLoadRelaxed(enqueued) + 1u;
  atomicStoreRelease(enqueued, queued.sequence);
  atomicStoreRelease(&g_commandWriteIndex, writeIndex + 1);
  return true;
}

static bool dequeueCommand(AudioCommand& command)
{
  const uint32_t readIndex = atomicLoadRelaxed(&g_commandReadIndex);
  const uint32_t writeIndex = atomicLoadAcquire(&g_commandWriteIndex);
  if(readIndex == writeIndex)
  {
    return false;
  }

  command = g_commandQueue[readIndex % kCommandQueueLength];
  atomicStoreRelease(&g_commandReadIndex, readIndex + 1);
  return true;
}

static void beginVoiceMixBlock(MixerVoice& voice, uint8_t voiceChannel)
{
  voice.ringBlockActive = false;
  if(!voice.active || voice.source != VoiceSource::Sd)
  {
    return;
  }

  StreamRingControl& ring = g_ringControl[voiceChannel];
  const uint32_t generation = atomicLoadAcquire(&ring.generation);
  if(generation != voice.generation)
  {
    voice.active = false;
    return;
  }

  // Snapshot the producer boundary once for this 128-frame output block.
  // The producer may append more data meanwhile; it becomes visible in the
  // next block. This removes acquire/release barriers from every sample.
  voice.ringBlockGeneration = generation;
  voice.ringBlockReadIndex = atomicLoadRelaxed(&ring.readIndex);
  voice.ringBlockWriteIndex = atomicLoadAcquire(&ring.writeIndex);
  voice.ringBlockEof = atomicLoadAcquire(&ring.eof) != 0;
  voice.ringBlockActive = true;
}

static void endVoiceMixBlock(MixerVoice& voice, uint8_t voiceChannel)
{
  if(!voice.ringBlockActive)
  {
    return;
  }

  StreamRingControl& ring = g_ringControl[voiceChannel];
  if(atomicLoadAcquire(&ring.generation) == voice.ringBlockGeneration)
  {
    // Publish all frames consumed by this voice with one release operation.
    atomicStoreRelease(&ring.readIndex, voice.ringBlockReadIndex);
  }
  else
  {
    voice.active = false;
  }
  voice.ringBlockActive = false;
}

static SourceReadResult readSourceFrame(MixerVoice& voice,
                                        uint8_t voiceChannel,
                                        StereoFrame& destination)
{
  if(voice.source == VoiceSource::Array)
  {
    if(voice.sourceCursor >= voice.frameCount)
    {
      return SourceReadResult::End;
    }

    const uint32_t offset = voice.sourceCursor * voice.channelCount;
    destination.left = voice.arraySamples[offset];
    destination.right = voice.channelCount == 2
                      ? voice.arraySamples[offset + 1]
                      : destination.left;
    voice.sourceCursor++;
    return SourceReadResult::Available;
  }

  if(voice.source != VoiceSource::Sd)
  {
    return SourceReadResult::End;
  }

  StreamRingControl& ring = g_ringControl[voiceChannel];
  if(!voice.ringBlockActive ||
     atomicLoadRelaxed(&ring.generation) != voice.ringBlockGeneration)
  {
    return SourceReadResult::End;
  }

  if(voice.ringBlockReadIndex == voice.ringBlockWriteIndex)
  {
    return voice.ringBlockEof
         ? SourceReadResult::End
         : SourceReadResult::Waiting;
  }

  destination =
    g_sdPcm[voiceChannel][voice.ringBlockReadIndex & kSdRingMask];
  voice.ringBlockReadIndex++;
  return SourceReadResult::Available;
}

static void reportVoiceStarvation(MixerVoice& voice)
{
  if(!voice.starvationReported)
  {
    __atomic_add_fetch(&g_streamStarvations, 1u, __ATOMIC_RELAXED);
    voice.starvationReported = true;
  }
}

static bool primeVoice(MixerVoice& voice, uint8_t voiceChannel)
{
  if(!voice.haveSample0)
  {
    const SourceReadResult result = readSourceFrame(voice, voiceChannel, voice.sample0);
    if(result == SourceReadResult::Waiting)
    {
      reportVoiceStarvation(voice);
      return false;
    }
    if(result == SourceReadResult::End)
    {
      voice.active = false;
      return false;
    }
    voice.haveSample0 = true;
  }

  if(!voice.haveSample1)
  {
    const SourceReadResult result = readSourceFrame(voice, voiceChannel, voice.sample1);
    if(result == SourceReadResult::Waiting)
    {
      reportVoiceStarvation(voice);
      return false;
    }
    if(result == SourceReadResult::End)
    {
      voice.sample1 = voice.sample0;
      voice.endAfterSample1 = true;
    }
    voice.haveSample1 = true;
  }

  voice.starvationReported = false;
  return true;
}

static void advanceVoice(MixerVoice& voice, uint8_t voiceChannel)
{
  static constexpr uint32_t kOneQ16 = 1u << 16;
  voice.phaseQ16 += voice.phaseStepQ16;

  while(voice.active && voice.phaseQ16 >= kOneQ16)
  {
    voice.phaseQ16 -= kOneQ16;
    if(voice.playbackFrame < voice.totalFrames) ++voice.playbackFrame;
    voice.sample0 = voice.sample1;
    voice.haveSample0 = true;

    if(voice.endAfterSample1)
    {
      voice.active = false;
      break;
    }

    voice.haveSample1 = false;
    const SourceReadResult result = readSourceFrame(voice, voiceChannel, voice.sample1);
    if(result == SourceReadResult::Available)
    {
      voice.haveSample1 = true;
      voice.starvationReported = false;
    }
    else if(result == SourceReadResult::End)
    {
      voice.sample1 = voice.sample0;
      voice.haveSample1 = true;
      voice.endAfterSample1 = true;
    }
    else
    {
      // Pause the source clock while its producer catches up. This avoids
      // consuming time from a voice whose SD data is temporarily unavailable.
      voice.phaseQ16 = 0;
      reportVoiceStarvation(voice);
      break;
    }
  }
}

static bool mixVoiceFrame(MixerVoice& voice,
                          uint8_t voiceChannel,
                          int32_t& mixedLeft,
                          int32_t& mixedRight)
{
  if(!voice.active || !primeVoice(voice, voiceChannel))
  {
    return false;
  }

  // Q15 keeps the interpolation products inside signed 32 bits even for the
  // full -65535..65535 sample delta, avoiding per-sample 64-bit arithmetic.
  const uint32_t fractionQ15 = voice.phaseQ16 >> 1;
  const int32_t leftDelta = (int32_t)voice.sample1.left - voice.sample0.left;
  const int32_t rightDelta = (int32_t)voice.sample1.right - voice.sample0.right;
  const int32_t sourceLeft = voice.sample0.left
                           + ((leftDelta * (int32_t)fractionQ15) >> 15);
  const int32_t sourceRight = voice.sample0.right
                            + ((rightDelta * (int32_t)fractionQ15) >> 15);

  // Every public voice currently uses unity gain. Bypass two more 64-bit
  // multiplies in that common path; retain the general path for future volume
  // controls.
  if(voice.gainLeftQ15 == 32767 && voice.gainRightQ15 == 32767)
  {
    mixedLeft += sourceLeft;
    mixedRight += sourceRight;
  }
  else
  {
    mixedLeft += (int32_t)(((int64_t)sourceLeft * voice.gainLeftQ15) >> 15);
    mixedRight += (int32_t)(((int64_t)sourceRight * voice.gainRightQ15) >> 15);
  }

  advanceVoice(voice, voiceChannel);
  return true;
}

static int16_t clampToInt16(int32_t value)
{
  if(value > 32767)
  {
    return 32767;
  }
  if(value < -32768)
  {
    return -32768;
  }
  return (int16_t)value;
}

static bool mixOutputBlock(uint32_t voiceMask)
{
  memset(g_mixAccumulator, 0, sizeof(g_mixAccumulator));
  bool producedAnySample = false;
  // Process one complete block per active voice. Each voice therefore walks
  // its PSRAM ring sequentially instead of switching rings for
  // every output frame. Inactive channels do not enter the hot loop at all.
  while(voiceMask != 0)
  {
    const uint8_t voiceChannel = (uint8_t)__builtin_ctz(voiceMask);
    voiceMask &= voiceMask - 1u;
    MixerVoice& voice = g_voices[voiceChannel];
    beginVoiceMixBlock(voice, voiceChannel);

    for(uint16_t frame = 0; frame < kMixBlockFrames; frame++)
    {
      producedAnySample |= mixVoiceFrame(voice,
                                         voiceChannel,
                                         g_mixAccumulator[frame].left,
                                         g_mixAccumulator[frame].right);
    }

    endVoiceMixBlock(voice, voiceChannel);
  }

  for(uint16_t frame = 0; frame < kMixBlockFrames; frame++)
  {
    const uint16_t left =
      (uint16_t)clampToInt16(g_mixAccumulator[frame].left);
    const uint16_t right =
      (uint16_t)clampToInt16(g_mixAccumulator[frame].right);
    g_mixBlock[frame] = ((uint32_t)left << 16) | right;
  }

  return producedAnySample;
}

static void refreshActiveVoiceMask(uint32_t changedVoices)
{
  uint32_t mask = atomicLoadRelaxed(&g_activeVoiceMask) & ~changedVoices;
  while(changedVoices != 0)
  {
    const uint8_t voiceChannel = (uint8_t)__builtin_ctz(changedVoices);
    changedVoices &= changedVoices - 1u;
    VoicePositionSnapshot &snapshot = g_voicePositions[voiceChannel];
    const MixerVoice &voice = g_voices[voiceChannel];
    if(voice.active)
    {
      mask |= 1u << voiceChannel;
    }
    __atomic_fetch_add(&snapshot.sequence, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&snapshot.currentFrame, voice.playbackFrame, __ATOMIC_RELAXED);
    __atomic_store_n(&snapshot.totalFrames, voice.totalFrames, __ATOMIC_RELAXED);
    __atomic_store_n(&snapshot.sampleRate, voice.sampleRate, __ATOMIC_RELAXED);
    __atomic_store_n(&snapshot.active, voice.active ? 1u : 0u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&snapshot.sequence, 1u, __ATOMIC_RELEASE);
  }
  atomicStoreRelease(&g_activeVoiceMask, mask);
}

static void resetMixerVoice(MixerVoice& voice)
{
  voice = MixerVoice{};
  voice.gainLeftQ15 = 32767;
  voice.gainRightQ15 = 32767;
}

static void processAudioCommands()
{
  AudioCommand command;
  uint32_t changedVoices = 0;
  uint32_t applied[AUDIO_VOICE_CHANNELS];
  while(dequeueCommand(command))
  {
    changedVoices |= 1u << command.voiceChannel;
    applied[command.voiceChannel] = command.sequence;
    MixerVoice& voice = g_voices[command.voiceChannel];
    resetMixerVoice(voice);

    if(command.type == AudioCommandType::Stop)
    {
      continue;
    }

    if(command.sampleRate < 8000 || command.sampleRate > 192000)
    {
      continue;
    }

    voice.active = true;
    voice.sampleRate = command.sampleRate;
    voice.playbackFrame = command.startFrame;
    voice.totalFrames = command.totalFrames;
    voice.phaseStepQ16 = (uint32_t)(((uint64_t)command.sampleRate << 16)
                                  / AUDIO_OUTPUT_SAMPLE_RATE);

    if(command.type == AudioCommandType::PlaySd)
    {
      voice.source = VoiceSource::Sd;
      voice.channelCount = 2;
      voice.generation = command.generation;
    }
    else
    {
      if(command.arraySamples == nullptr || command.frameCount == 0 ||
         (command.channelCount != 1 && command.channelCount != 2))
      {
        resetMixerVoice(voice);
        continue;
      }

      voice.source = VoiceSource::Array;
      voice.channelCount = command.channelCount;
      voice.arraySamples = command.arraySamples;
      voice.frameCount = command.frameCount;
    }
  }

  if(changedVoices == 0) return;

  refreshActiveVoiceMask(changedVoices);
  while(changedVoices != 0)
  {
    const uint8_t voiceChannel = (uint8_t)__builtin_ctz(changedVoices);
    changedVoices &= changedVoices - 1u;
    atomicStoreRelease(&g_voiceCommandApplied[voiceChannel], applied[voiceChannel]);
  }
}

static bool submitPendingMixBlock()
{
  if(g_pendingByteCount == 0)
  {
    return true;
  }

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(g_mixBlock);
  const size_t accepted = g_pcm5102.write(bytes + g_pendingByteOffset,
                                          g_pendingByteCount);
  g_pendingByteOffset += accepted;
  g_pendingByteCount -= accepted;

  if(g_pendingByteCount == 0)
  {
    g_pendingByteOffset = 0;
    return true;
  }
  return false;
}
}

bool audio_runtime_init()
{
  if(get_core_num() != 0)
  {
    return false;
  }

  if(atomicLoadAcquire(&g_runtimeReady) != 0)
  {
    return true;
  }

  atomicStoreRelease(&g_commandReadIndex, 0);
  atomicStoreRelease(&g_commandWriteIndex, 0);
  atomicStoreRelease(&g_dmaStreamMask, 0);

  for(uint8_t voiceChannel = 0; voiceChannel < AUDIO_VOICE_CHANNELS; voiceChannel++)
  {
    g_sdStreams[voiceChannel].active = false;
    g_sdStreams[voiceChannel].dmaStreaming = false;
    g_sdStreams[voiceChannel].generation = nextGeneration();
    resetStreamRing(voiceChannel, g_sdStreams[voiceChannel].generation, true);
    resetMixerVoice(g_voices[voiceChannel]);
  }

  g_sdDma = SdDmaController{};
  g_sdDma.state = SdDmaState::Idle;
  atomicStoreRelease(&g_runtimeReady, 1u);
  g_nextCore0ServiceUs = micros();
  return true;
}

bool audio_runtime_ready()
{
  return atomicLoadAcquire(&g_runtimeReady) != 0;
}

bool audio_output_ready()
{
  return atomicLoadAcquire(&g_core1Started) != 0;
}

bool audio_core1_begin()
{
  if(get_core_num() != 1 || !audio_runtime_ready())
  {
    return false;
  }

  if(atomicLoadAcquire(&g_core1Started) != 0)
  {
    return true;
  }

  const uint32_t now = micros();
  if(g_nextI2sHeapRetryUs && (int32_t)(now - g_nextI2sHeapRetryUs) < 0) return false;
  if(rp2040.getFreeHeap() < (int32_t)kI2sMinimumStartupHeap)
  {
    // Keep returning to loop1's keyboard polling. Retry slowly if temporary
    // startup pressure later clears; never enter unchecked I2S allocation.
    g_nextI2sHeapRetryUs = now + 1000000u;
    return false;
  }

  bool configured = true;
  configured &= g_pcm5102.setBCLK(kBclkPin);
  configured &= g_pcm5102.setDATA(kDataPin);
  configured &= g_pcm5102.setBuffers(kI2sBufferCount, kI2sBufferWords);
  configured &= g_pcm5102.setBitsPerSample(16);

  if(!configured || !g_pcm5102.begin(AUDIO_OUTPUT_SAMPLE_RATE))
  {
    return false;
  }

  // I2S just reserved its ping-pong pair. Keep one RX/TX pair for SPI audio
  // for the remainder of runtime. The GPIO GPU driver uses no DMA channels.
  // Polling them avoids the Arduino SPI and audio-buffer shared IRQ handlers.
  initializeAudioSpiDma();

  atomicStoreRelease(&g_core1Started, 1u);
  return true;
}

void audio_core0_poll()
{
  if(get_core_num() != 0 || !audio_runtime_ready())
  {
    return;
  }

  // Direct callers and cooperative NARM callers share the same deadline.
  // Signed subtraction keeps the comparison correct across micros() wrap.
  g_nextCore0ServiceUs = micros() + kCore0ServiceIntervalUs;

  // Core 1 may currently own SPI0 for an asynchronous audio burst. Never make
  // the NARM wait here; fragmented-file fallback can resume on a later poll.
  SdBusGuard busGuard(true);
  if(!busGuard)
  {
    return;
  }

  // Refill at most one voice per call. Large 4096-frame transactions preserve
  // SD multi-block efficiency, while the 1 ms cooperative service cadence
  // spreads different voices across program execution instead of blocking
  // Core 0 on every active file back-to-back.
  for(uint8_t checked = 0; checked < AUDIO_VOICE_CHANNELS; checked++)
  {
    const uint8_t voiceChannel = g_sdRoundRobinCursor;
    g_sdRoundRobinCursor = (uint8_t)((g_sdRoundRobinCursor + 1)
                                  % AUDIO_VOICE_CHANNELS);
    if(g_sdStreams[voiceChannel].active &&
       !g_sdStreams[voiceChannel].dmaStreaming &&
       fillSdStreamChunk(voiceChannel))
    {
      break;
    }
  }
}

void audio_core0_poll_if_due()
{
  if(get_core_num() != 0 || !audio_runtime_ready())
  {
    return;
  }

  const uint32_t now = micros();
  if((int32_t)(now - g_nextCore0ServiceUs) < 0)
  {
    return;
  }

  audio_core0_poll();
}

void audio_core1_poll()
{
  if(get_core_num() != 1 || atomicLoadAcquire(&g_core1Started) == 0)
  {
    return;
  }

  serviceSdDma();
  processAudioCommands();

  if(g_pcm5102.getUnderflow() && atomicLoadAcquire(&g_activeVoiceMask) != 0)
  {
    __atomic_add_fetch(&g_outputUnderflows, 1u, __ATOMIC_RELAXED);
  }

  for(uint8_t block = 0; block < kMaxMixBlocksPerPoll; block++)
  {
    if(!submitPendingMixBlock())
    {
      break;
    }

    const uint32_t mixingVoices = atomicLoadRelaxed(&g_activeVoiceMask) &
                                  ~atomicLoadAcquire(&g_pausedVoiceMask);
    if(mixingVoices == 0)
    {
      break;
    }

    if(g_pcm5102.availableForWrite() < (int)sizeof(g_mixBlock))
    {
      break;
    }

    const bool produced = mixOutputBlock(mixingVoices);
    refreshActiveVoiceMask(mixingVoices);
    if(!produced)
    {
      break;
    }

    g_pendingByteOffset = 0;
    g_pendingByteCount = sizeof(g_mixBlock);
  }

  serviceSdDma();
}

bool play_sound_SD(uint8_t voiceChannel, const String& fileName)
{
  return play_sound_SD_from_frame(voiceChannel, fileName, 0u);
}

bool play_sound_SD_from_frame(uint8_t voiceChannel, const String& fileName,
                              uint32_t startFrame)
{
  if(get_core_num() != 0 || !audio_runtime_ready() || !audio_output_ready() ||
     voiceChannel >= AUDIO_VOICE_CHANNELS || fileName.length() == 0)
  {
    return false;
  }

  SdBusGuard busGuard;
  closeSdStream(voiceChannel);
  const uint32_t generation = nextGeneration();
  resetStreamRing(voiceChannel, generation, false);

  if(sdEnsureReady() != 1)
  {
    resetStreamRing(voiceChannel, generation, true);
    return false;
  }

  SdStream& stream = g_sdStreams[voiceChannel];
  stream.file = SD.open(fileName.c_str(), FIRMWARE_FILE_READ);
  if(!stream.file)
  {
    resetStreamRing(voiceChannel, generation, true);
    return false;
  }

  Sector_t contiguousFirstSector = 0;
  Sector_t contiguousLastSector = 0;
  const bool contiguousFile =
    stream.file.contiguousRange(&contiguousFirstSector, &contiguousLastSector);

  WaveInfo info{};
  if(!parseWaveFile(stream.file, info))
  {
    stream.file.close();
    resetStreamRing(voiceChannel, generation, true);
    return false;
  }

  const uint32_t totalFrames = info.dataBytes / info.blockAlign;
  if(startFrame >= totalFrames)
  {
    stream.file.close();
    resetStreamRing(voiceChannel, generation, true);
    return false;
  }
  const uint64_t startByte = (uint64_t)info.dataOffset
                           + (uint64_t)startFrame * info.blockAlign;
  if(startByte > UINT32_MAX || !stream.file.seekSet((uint32_t)startByte))
  {
    stream.file.close();
    resetStreamRing(voiceChannel, generation, true);
    return false;
  }

  stream.active = true;
  stream.dmaStreaming = false;
  stream.channels = info.channels;
  stream.bitsPerSample = info.bitsPerSample;
  stream.blockAlign = info.blockAlign;
  stream.sampleRate = info.sampleRate;
  stream.framesRemaining = totalFrames - startFrame;
  stream.generation = generation;
  stream.partialFrameBytes = 0;

  // Short effects that fit in their ring are decoded completely now. They no
  // longer consume SD bandwidth or Core-0 time while playing. Long tracks keep
  // the existing half-ring startup target and stream in the background.
  const uint32_t prefillTarget = stream.framesRemaining <= kSdRingFrames
                               ? stream.framesRemaining
                               : kStartPrefillFrames;
  while(stream.active)
  {
    const uint32_t buffered = atomicLoadAcquire(&g_ringControl[voiceChannel].writeIndex)
                            - atomicLoadAcquire(&g_ringControl[voiceChannel].readIndex);
    if(buffered >= prefillTarget || !fillSdStreamChunk(voiceChannel))
    {
      break;
    }
  }

  if(atomicLoadAcquire(&g_ringControl[voiceChannel].writeIndex) == 0)
  {
    finishSdStream(voiceChannel);
    return false;
  }

  // Once a long WAV has its startup cushion, contiguous files no longer need
  // synchronous SdFat reads. Core 1 reads the remaining physical sectors with
  // SPI DMA. Fragmented files safely retain the existing Core-0 fallback.
  if(contiguousFile && stream.active && stream.framesRemaining != 0)
  {
    const uint64_t fileBytePosition = stream.file.curPosition();
    const uint64_t bytesRemaining =
      (uint64_t)stream.framesRemaining * stream.blockAlign;
    const uint64_t finalFileByte = fileBytePosition + bytesRemaining - 1u;
    const Sector_t nextSector = contiguousFirstSector
                              + (Sector_t)(fileBytePosition / kSdSectorBytes);
    const Sector_t finalSector = contiguousFirstSector
                               + (Sector_t)(finalFileByte / kSdSectorBytes);

    if(nextSector <= contiguousLastSector && finalSector <= contiguousLastSector)
    {
      stream.dmaNextSector = nextSector;
      stream.dmaLastSector = finalSector;
      stream.dmaBytesRemaining = (uint32_t)bytesRemaining;
      stream.dmaSectorOffset =
        (uint16_t)(fileBytePosition % kSdSectorBytes);
      stream.dmaStreaming = true;
      stream.file.close();
      __atomic_fetch_or(&g_dmaStreamMask,
                        1u << voiceChannel,
                        __ATOMIC_RELEASE);
    }
  }

  AudioCommand command{};
  command.type = AudioCommandType::PlaySd;
  command.voiceChannel = voiceChannel;
  command.channelCount = 2;
  command.generation = generation;
  command.sampleRate = info.sampleRate;
  command.startFrame = startFrame;
  command.totalFrames = totalFrames;

  if(!enqueueCommand(command))
  {
    invalidateSdStream(voiceChannel);
    return false;
  }

  return true;
}

bool play_sound_array(uint8_t voiceChannel,
                      const int16_t* samples,
                      uint32_t frameCount,
                      uint32_t sampleRate,
                      uint8_t channelCount)
{
  return play_sound_array_from_frame(voiceChannel, samples, frameCount, 0u,
                                     sampleRate, channelCount);
}

bool play_sound_array_from_frame(uint8_t voiceChannel,
                                 const int16_t* samples,
                                 uint32_t frameCount,
                                 uint32_t startFrame,
                                 uint32_t sampleRate,
                                 uint8_t channelCount)
{
  if(get_core_num() != 0 || !audio_runtime_ready() || !audio_output_ready() ||
     voiceChannel >= AUDIO_VOICE_CHANNELS || samples == nullptr ||
     frameCount == 0 || startFrame >= frameCount ||
     sampleRate < 8000 || sampleRate > 192000 ||
     (channelCount != 1 && channelCount != 2))
  {
    return false;
  }

  const uint64_t sampleOffset = (uint64_t)startFrame * channelCount;
  if(sampleOffset > SIZE_MAX / sizeof(int16_t))
  {
    return false;
  }

  SdBusGuard busGuard;
  const uint32_t generation = invalidateSdStream(voiceChannel);

  AudioCommand command{};
  command.type = AudioCommandType::PlayArray;
  command.voiceChannel = voiceChannel;
  command.channelCount = channelCount;
  command.generation = generation;
  command.sampleRate = sampleRate;
  command.frameCount = frameCount - startFrame;
  command.startFrame = startFrame;
  command.totalFrames = frameCount;
  command.arraySamples = samples + (size_t)sampleOffset;
  return enqueueCommand(command);
}

bool stop_voice_channel(uint8_t voiceChannel)
{
  if(get_core_num() != 0 || !audio_runtime_ready() ||
     voiceChannel >= AUDIO_VOICE_CHANNELS)
  {
    return false;
  }

  SdBusGuard busGuard;
  const uint32_t generation = invalidateSdStream(voiceChannel);
  AudioCommand command{};
  command.type = AudioCommandType::Stop;
  command.voiceChannel = voiceChannel;
  command.generation = generation;
  return enqueueCommand(command);
}

void stop_all_voice_channels()
{
  if(get_core_num() != 0 || !audio_runtime_ready())
  {
    return;
  }

  for(uint8_t voiceChannel = 0; voiceChannel < AUDIO_VOICE_CHANNELS; voiceChannel++)
  {
    stop_voice_channel(voiceChannel);
  }
}

bool voice_channel_active(uint8_t voiceChannel)
{
  if(voiceChannel >= AUDIO_VOICE_CHANNELS)
  {
    return false;
  }
  return (atomicLoadAcquire(&g_activeVoiceMask) & (1u << voiceChannel)) != 0;
}

bool audio_set_voice_paused(uint8_t voiceChannel, bool paused)
{
  if(voiceChannel >= AUDIO_VOICE_CHANNELS)
  {
    return false;
  }
  const uint32_t bit = 1u << voiceChannel;
  if(paused)
  {
    __atomic_fetch_or(&g_pausedVoiceMask, bit, __ATOMIC_ACQ_REL);
  }
  else
  {
    __atomic_fetch_and(&g_pausedVoiceMask, ~bit, __ATOMIC_ACQ_REL);
  }
  return true;
}

bool audio_get_voice_position(uint8_t voiceChannel, AudioVoicePosition &position)
{
  if(voiceChannel >= AUDIO_VOICE_CHANNELS || !audio_runtime_ready()) return false;
  const VoicePositionSnapshot &snapshot = g_voicePositions[voiceChannel];
  for(uint32_t attempt = 0; attempt < 8u; ++attempt)
  {
    const uint32_t sequence = atomicLoadAcquire(&snapshot.sequence);
    if((sequence & 1u) != 0u) continue;
    AudioVoicePosition result{};
    result.current_frame = atomicLoadRelaxed(&snapshot.currentFrame);
    result.total_frames = atomicLoadRelaxed(&snapshot.totalFrames);
    result.sample_rate = atomicLoadRelaxed(&snapshot.sampleRate);
    result.active = atomicLoadRelaxed(&snapshot.active) != 0u;
    result.paused = (atomicLoadAcquire(&g_pausedVoiceMask) & (1u << voiceChannel)) != 0u;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(sequence == atomicLoadAcquire(&snapshot.sequence))
    {
      position = result;
      return true;
    }
  }
  return false;
}

uint32_t audio_output_underflow_count()
{
  return atomicLoadRelaxed(&g_outputUnderflows);
}

uint32_t audio_stream_starvation_count()
{
  return atomicLoadRelaxed(&g_streamStarvations);
}

bool audio_voice_commands_idle(uint8_t voiceChannel)
{
  return voiceChannel < AUDIO_VOICE_CHANNELS &&
         atomicLoadAcquire(&g_voiceCommandApplied[voiceChannel]) ==
         atomicLoadRelaxed(&g_voiceCommandEnqueued[voiceChannel]);
}
