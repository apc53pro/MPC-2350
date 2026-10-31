#ifndef SDMGR_H
#define SDMGR_H

#include <stdint.h>

// SdFat and Arduino SPI are not safe to enter concurrently from both RP2350
// cores.  Normal filesystem users take this recursive lock for the complete
// lifetime of each operation.  The audio DMA engine uses the non-blocking form
// and holds the lock while an SD multi-block command is active.
void sdBusAcquire();
bool sdBusTryAcquire();
void sdBusRelease();

class SdBusGuard
{
public:
  SdBusGuard() : held_(true) { sdBusAcquire(); }
  explicit SdBusGuard(bool tryOnly)
    : held_(tryOnly ? sdBusTryAcquire() : true)
  {
    if(!tryOnly)
    {
      sdBusAcquire();
    }
  }
  ~SdBusGuard()
  {
    if(held_)
    {
      sdBusRelease();
    }
  }

  explicit operator bool() const { return held_; }
  SdBusGuard(const SdBusGuard&) = delete;
  SdBusGuard& operator=(const SdBusGuard&) = delete;

private:
  bool held_;
};

int8_t sdEnsureReady();

#endif 
