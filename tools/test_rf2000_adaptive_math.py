#!/usr/bin/env python3
"""Exercise the production adaptive-grid helper without an AVR or hardware."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rf2000_adaptive_math.h"

static unsigned long cases;

static void verify(int32_t ideal, int32_t physical, unsigned phase,
                   unsigned shift, bool negative, bool orientation)
{
  rf_adaptive_axis_t result;
  assert(rf_adaptive_axis_plan(ideal,physical,phase,shift,negative,orientation,&result));
  const int64_t distance = negative ? (int64_t)physical-ideal :
    (int64_t)ideal-physical;
  const int64_t advance = negative ? (int64_t)physical-result.position :
    (int64_t)result.position-physical;
  const unsigned stride = 1u<<shift;
  const int phase_sign = orientation != negative ? 1 : -1;
  assert(advance >= 0); /* No invented reversal, including a target behind us. */
  if (distance <= 0) {
    assert(!result.pulses && !result.first_delta && !advance);
    assert(result.phase == phase);
  } else {
    assert(advance <= distance); /* No overshoot at a MODE boundary. */
    assert(distance-advance < stride);
    if (!result.pulses) {
      assert(!advance && !result.first_delta && result.phase == phase);
    } else {
      assert(result.first_delta >= 1 && result.first_delta <= stride);
      assert(advance == result.first_delta+(int64_t)(result.pulses-1)*stride);
      /* First edge lands on the next valid state, not a later one. */
      for (unsigned k=1; k<result.first_delta; ++k)
        assert(((unsigned)(phase+phase_sign*(int)k)&(stride-1u)) != 0);
      assert(((unsigned)(phase+phase_sign*result.first_delta)&(stride-1u)) == 0);
      assert((result.phase&(stride-1u)) == 0);
      assert(result.phase == (uint8_t)(phase+phase_sign*advance));
      if (!shift) assert(result.position == ideal);
    }
  }
  ++cases;
}

static void exhaustive(void)
{
  for (unsigned phase=0; phase<256; ++phase)
    for (unsigned shift=0; shift<=RF_ADAPTIVE_MAX_SHIFT; ++shift)
      for (unsigned negative=0; negative<2; ++negative)
        for (unsigned orientation=0; orientation<2; ++orientation)
          for (int distance=-(int)(2u<<shift); distance<=(int)(4u<<shift)+2; ++distance) {
            const int32_t physical=12345;
            const int32_t ideal=negative ? physical-distance : physical+distance;
            verify(ideal,physical,phase,shift,negative,orientation);
          }
}

static void sequences(void)
{
  /* Carry the physical residual through coarse/fine changes and true reversals. */
  for (unsigned phase=0; phase<256; ++phase)
    for (unsigned orientation=0; orientation<2; ++orientation) {
      int32_t ideal=-800, physical=ideal;
      uint8_t actual_phase=phase;
      for (unsigned leg=0; leg<4; ++leg) {
        const bool negative=(leg&1u) != 0;
        for (unsigned step=0; step<240; ++step) {
          const unsigned shift=(step/20u)%6u;
          ideal += negative ? -(int)(1u+step%17u) : (int)(1u+step%17u);
          verify(ideal,physical,actual_phase,shift,negative,orientation);
          rf_adaptive_axis_t result;
          assert(rf_adaptive_axis_plan(ideal,physical,actual_phase,shift,
                                      negative,orientation,&result));
          physical=result.position;
          actual_phase=result.phase;
        }
        /* A full-stop fine tail must remove all carried residual exactly. */
        rf_adaptive_axis_t tail;
        assert(rf_adaptive_axis_plan(ideal,physical,actual_phase,0,
                                    negative,orientation,&tail));
        physical=tail.position;
        actual_phase=tail.phase;
        assert(physical == ideal);
      }
    }
}

static void limits(void)
{
  rf_adaptive_axis_t output={123,7,42,99}, saved=output;
  assert(!rf_adaptive_axis_plan(0,0,0,6,false,true,&output));
  assert(!memcmp(&output,&saved,sizeof output));
  assert(!rf_adaptive_axis_plan(0,0,0,0,false,true,NULL));
  assert(!rf_adaptive_axis_plan(INT32_MAX,INT32_MIN,0,5,false,true,&output));
  assert(!memcmp(&output,&saved,sizeof output));
  assert(!rf_adaptive_axis_plan(INT32_MIN,INT32_MAX,0,5,true,true,&output));
  assert(!memcmp(&output,&saved,sizeof output));
  for (unsigned shift=0; shift<=5; ++shift) {
    const int32_t maximum=(int32_t)(UINT16_MAX*(1UL<<shift));
    assert(rf_adaptive_axis_plan(maximum,0,0,shift,false,true,&output));
    assert(output.pulses == UINT16_MAX && output.position == maximum);
    saved=output;
    assert(!rf_adaptive_axis_plan(maximum+(1u<<shift),0,0,shift,false,true,&output));
    assert(!memcmp(&output,&saved,sizeof output));
    verify(INT32_MAX,INT32_MAX-1,255,shift,false,true);
    verify(INT32_MIN,INT32_MIN+1,0,shift,true,false);
  }
}

static void selection(void)
{
  assert(rf_adaptive_select_shift(NAN,0) == 5);
  assert(rf_adaptive_select_shift(INFINITY,0) == 5);
  assert(rf_adaptive_select_shift(-1,0) == 5);
  assert(rf_adaptive_select_shift(0,255) == 0);
  assert(rf_adaptive_select_shift(12000,0) == 0);
  assert(rf_adaptive_select_shift(12001,0) == 1);
  assert(rf_adaptive_select_shift(9001,1) == 1);
  assert(rf_adaptive_select_shift(9000,1) == 0);
  assert(rf_adaptive_select_shift(20480.0f*1000.0f/60.0f,0) == 5);
  for (unsigned shift=0; shift<=5; ++shift) {
    const float rate=10000.0f*(1u<<shift);
    assert(rf_adaptive_select_shift(rate,0) == shift);
    assert(rf_adaptive_select_shift(rate,shift) == shift);
    assert(rf_adaptive_select_shift(12000.0f*(1u<<shift),shift) == shift);
    if (shift) {
      assert(rf_adaptive_select_shift(9000.0f*(1u<<(shift-1)),shift) == shift-1);
      assert(rf_adaptive_select_shift(9001.0f*(1u<<(shift-1)),shift) == shift);
    }
  }
  /* All legal rates stay within the hardware-pulse budget for all prior modes. */
  for (unsigned previous=0; previous<=5; ++previous)
    for (unsigned rate=0; rate<=384000; rate+=37) {
      unsigned selected=rf_adaptive_select_shift(rate,previous);
      assert(selected<=5 && rate <= 12000u*(1u<<selected));
    }
}

int main(void)
{
  exhaustive();
  sequences();
  limits();
  selection();
  printf("PASS: production adaptive math; %lu phase/direction/stride cases, "
         "coarse/fine residuals, exact stop tails, overflow guards and hysteresis\n",cases);
  return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="rf2000-adaptive-math-") as temporary:
        directory = Path(temporary)
        source = directory / "test.c"
        binary = directory / "test"
        source.write_text(SOURCE)
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-O2", "-g",
            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-iquote", str(ROOT / "grbl"),
            str(source), "-lm", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
