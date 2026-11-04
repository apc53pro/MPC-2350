#ifndef RP2350_QMI_TIMING_H
#define RP2350_QMI_TIMING_H

#include <stdint.h>

struct Rp2350PsramCsTiming
{
  uint32_t max_select;
  uint32_t min_deselect;

  constexpr bool valid() const
  {
    return max_select > 0u && max_select <= 63u && min_deselect <= 31u;
  }
};

// Match the installed core's PSRAM requirements: CS high >=18 ns and
// CS low <=8 us. MAX_SELECT can expire during an access; allow that access
// to finish. QPI read: 2 command + 6 address + 6 dummy + 16 data SCK cycles
// for the RP2350 XIP cache's 8-byte line. The 12 additional system cycles
// cover setup, hold and the RX pipeline at the configured RXDELAY=5.
constexpr Rp2350PsramCsTiming rp2350_psram_cs_timing(uint32_t sys_hz,
                                                    uint32_t divider)
{
  const uint32_t high_cycles = (uint32_t)(((uint64_t)sys_hz * 18u +
                                         999999999u) / 1000000000u);
  const uint32_t half_sck = (divider + 1u) / 2u;
  const uint32_t select_cycles = (uint32_t)((uint64_t)sys_hz * 8u / 1000000u);
  const uint32_t transfer_cycles = 30u * divider + 12u;
  const uint32_t max_select = select_cycles > transfer_cycles
      ? (select_cycles - transfer_cycles) / 64u : 0u;
  return {max_select > 63u ? 63u : max_select,
          high_cycles > half_sck ? high_cycles - half_sck : 0u};
}

// One configuration must meet the timing constraints on both sides of a
// PLL change: use the shorter assertion limit and longer deselect interval.
constexpr Rp2350PsramCsTiming rp2350_psram_cs_transition(uint32_t old_hz,
                                                        uint32_t new_hz,
                                                        uint32_t divider)
{
  const auto old_timing = rp2350_psram_cs_timing(old_hz, divider);
  const auto new_timing = rp2350_psram_cs_timing(new_hz, divider);
  return {old_timing.max_select < new_timing.max_select
            ? old_timing.max_select : new_timing.max_select,
          old_timing.min_deselect > new_timing.min_deselect
            ? old_timing.min_deselect : new_timing.min_deselect};
}

#endif
