/* RF2000 adaptive STEP arithmetic. GPL-3.0-or-later.
 *
 * Positions use the fixed 1/256 virtual grid. A hardware quantum of 1<<shift
 * therefore selects DRV8711 resolutions from 1/256 through 1/8. This file
 * neither accesses hardware nor changes the caller's direction or units.
 */
#ifndef RF2000_ADAPTIVE_MATH_H
#define RF2000_ADAPTIVE_MATH_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define RF_ADAPTIVE_MAX_SHIFT 5u
#define RF_ADAPTIVE_PULSE_HZ 12000u
#define RF_ADAPTIVE_REFINE_PULSE_HZ 9000u

typedef struct {
  uint16_t pulses;
  uint8_t first_delta;
  int32_t position;
  uint8_t phase;
} rf_adaptive_axis_t;

/* Coarsen above 12 kHz and refine only when the finer mode is <=9 kHz.
 * A caller must separately reject rates above the largest stride's budget;
 * returning MAX_SHIFT is not authorization to exceed that budget.
 */
static inline uint8_t rf_adaptive_select_shift(float fine_rate_hz,
                                               uint8_t previous_shift)
{
  if (!isfinite(fine_rate_hz) || fine_rate_hz < 0.0f)
    return RF_ADAPTIVE_MAX_SHIFT;
  uint8_t shift = previous_shift <= RF_ADAPTIVE_MAX_SHIFT ?
    previous_shift : RF_ADAPTIVE_MAX_SHIFT;
  while (shift < RF_ADAPTIVE_MAX_SHIFT &&
         fine_rate_hz > (float)(RF_ADAPTIVE_PULSE_HZ * (1UL << shift)))
    ++shift;
  while (shift && fine_rate_hz <=
         (float)(RF_ADAPTIVE_REFINE_PULSE_HZ * (1UL << (shift-1u))))
    --shift;
  return shift;
}

/* Select only forward states in the planner's direction, never an artificial
 * reverse correction. The first edge after changing MODE may advance fewer
 * than stride fine units, because DRV8711 advances to its next valid state.
 * Low phase bits suffice: every supported stride divides 256.
 *
 * A successful result contains the exact physical endpoint and phase after
 * its pulses. Positive directed remainder is <stride; a target behind the
 * physical position emits no pulses. A shift-zero tail reaches its target
 * exactly when that target lies in the requested direction. No output is
 * modified on overflow or invalid input.
 */
static inline bool rf_adaptive_axis_plan(int32_t ideal, int32_t physical,
                                         uint8_t phase, uint8_t shift,
                                         bool negative,
                                         bool positive_phase_for_positive_logical,
                                         rf_adaptive_axis_t *out)
{
  if (!out || shift > RF_ADAPTIVE_MAX_SHIFT) return false;
  rf_adaptive_axis_t result = { 0u, 0u, physical, phase };
  const int64_t directed = negative ? (int64_t)physical - ideal :
    (int64_t)ideal - physical;
  if (directed <= 0) {
    *out = result;
    return true;
  }

  const uint8_t stride = (uint8_t)(1u << shift);
  const uint8_t remainder = phase & (uint8_t)(stride-1u);
  const bool phase_positive = positive_phase_for_positive_logical != negative;
  const uint8_t first = phase_positive ? stride-remainder :
    (remainder ? remainder : stride);
  if (directed < first) {
    *out = result;
    return true;
  }

  /* Difference of two int32_t values is <=UINT32_MAX. The inexpensive
   * power-of-two shift avoids a 64-bit division in foreground preparation. */
  const uint32_t count = 1u + (((uint32_t)directed-first) >> shift);
  if (count > UINT16_MAX) return false;
  const uint32_t advance = first + ((uint32_t)count-1u) * stride;
  const int64_t endpoint = negative ? (int64_t)physical-advance :
    (int64_t)physical+advance;
  if (endpoint < INT32_MIN || endpoint > INT32_MAX) return false;

  result.pulses = (uint16_t)count;
  result.first_delta = first;
  result.position = (int32_t)endpoint;
  result.phase = (uint8_t)(phase_positive ? (uint32_t)phase+advance :
    (uint32_t)phase-advance);
  *out = result;
  return true;
}

#endif
