# RF2000 STEP/DIR timing

All RF2000 profiles use three logical Grbl axes. Five physical DRV8711 channels
are initialized by the driver module; the two reserve channels have no planner
axis and remain asleep throughout motion. Initialization temporarily wakes
their digital circuitry with CTRL.ENBL=0 to perform RESET and clear status;
their motor bridges remain disabled. The STEP implementation is guarded by
`CPU_MAP_2560_RENKFORCE_CTC_V3` and `N_AXIS == 3`.

The separate `rf2000_y_homing_test` profile further masks physical STEP output
to PF6 and limits driver enable to Y. It uses the same pulse timers and minimum
timing constraints; X/Z/A/B motor bridges remain disabled.

The current `rf2000_cnc` profile is identified by
`20261022-rf2000-cnc-speed`. It programs **fixed X/Y 1/4 and Z 1/16**
(320/320/1280 steps/mm for the specified SFU1605/2:1 mechanics) and limits
configured physical step rates to **18 kHz**. The standard/Y commissioning
profiles retain X/Y 1/32, their former mechanics and **5 kHz**. Adaptive
driver microstep switching during a move is not implemented.

All XYZ STEP pins share PORTF (PF0/PF6/PF3). X/Y direction pins PF1/PF7 and Y
SLEEPn PF2 share the same register. Each pulse edge therefore uses one masked
PORTF write. The upstream delayed-pulse path caches complete port values per
axis; applying that path to this board would overwrite other axis STEP bits,
direction bits or the enable signal. The RF2000 path retains only masked
STEP/DIR values and combines them with the **current** PORTF value inside
the interrupt; it never replays a complete cached PORTF image.

X/Y direction bits are written together; Z direction uses PL1. These writes
occur with interrupts disabled, before Timer0 is started. Every other RF2000
module that writes shared ports uses short atomic GPIO operations as well.
Timer1 reenables interrupts only after the port writes and pulse timer setup,
so Timer0 cannot interrupt the read/modify/write sequence.

## Common pulse constraints and legacy timer sequence

At the original 16 MHz clock, Timer0 runs at 2 MHz (0.5 us ticks). The
standard and Y commissioning profiles retain this sequence for every pulse:

1. Timer1 writes direction and starts Timer0 from zero. Compare A is set to
   `STEP_PULSE_DELAY * 2`, with the test default **5 us**.
2. Compare A checks driver readiness, FAULTn and pending reset/alarm before
   writing the simultaneous XYZ STEP edges.
3. Immediately after that edge, Timer0 is reloaded for the
   requested pulse width, **10 us** by default. Overflow clears all STEP pins
   in one masked write and stops Timer0.

There is no empirical “minus 2 us” timing correction in this board path.
Interrupt latency and setup instructions lengthen these minimum intervals.
The pulse timer starts its width interval after the actual edge, so latency in
the rising-edge interrupt cannot shorten the requested pulse. Timer0 is not
armed for AMASS ticks that contain no physical step.

All profiles accept `$0` from 10 to 20 us and independently clamp that
range in the pulse code. `$2=0` and `$4=0` are required: STEP is active HIGH,
and the physical nSLEEP polarity is fixed by the board. With the legacy
5-kHz physical-rate limit, AMASS can still execute Timer1 up to 16,000 times
per second because smoothing inserts ticks without a physical pulse.

## Optimized CNC timer sequence

The CNC path caches the applied direction bits, not a complete output-port
image. Segment loading marks a potential direction update. Timer1 rewrites
DIR only when the actual masked direction differs from the cached value.
That change leaves a pending setup flag until the **first physical STEP**
has used the 5-us Timer0 Compare A delay. AMASS ticks with no STEP do not
consume this flag.

Once DIR is stable, subsequent physical rising edges are written directly
by Timer1 after its driver/fault/reset checks. Timer0 is reloaded from the
actual rising edge and its overflow ISR still performs the falling edge.
This steady-state path therefore avoids the separate rising-edge interrupt
without removing the direction setup interval after a change. The delayed
first-edge path retains its own checks immediately before STEP rises.

CNC AMASS increments are prepared in foreground code and stored in each
segment. Segment loading copies them rather than performing variable
32-bit shifts inside the ISR. AMASS and Bresenham pulse-count semantics
remain active. No ISR performs software SPI or a driver wake-up delay.

The 18-kHz CNC limit is a nominal settings-admission limit, not a guarantee
that every individual measured interval is exactly 55.556 us. Interrupt
collisions introduce timing variation. If Timer1 reaches a new output update
before Timer0 completes the previous pulse, motion stops with alarm 3
instead of truncating a pulse or changing direction during it. The source
does not silently resume such a stopped motion.

Driver wake-up waits the original 25 ms in foreground context before Timer1
is enabled. Neither timer ISR performs SPI or wakes a driver. A fault or reset
cancels pending pulse edges, sleeps all five drivers and stops the pulse timer.
Emergency stopping can intentionally shorten a pulse already in progress;
minimum-width guarantees apply to ordinary motion, not emergency shutdown.

## Measured software evidence

The final CNC ELF SHA256 is
`0785cad168cbbb4a712c643ac81fab901868654282091ec0879c0968f5dee1f3`.
`tests/run-speed-sim.sh` exercises public UART settings, physical STEP/DIR
edges, exact counts and position, live LCD plus 100-Hz status requests,
direction reversals and fault/limit/reset/pause cases. The final run passed
15 cases at `$0=10`, plus an XYZ reverse case at `$0=20`.

| Requested rate | Pulse setting | XYZ plateau mean | Fastest/slowest 64-edge windows | Minimum HIGH | Minimum LOW |
| --- | --- | --- | --- | --- | --- |
| 18,000 steps/s | 10 us | 17,977.53 steps/s | 18,068.89 / 17,886.15 steps/s | 10.625 us | 24.250 us |
| 18,000 steps/s | 20 us | 17,977.51 steps/s | 18,070.16 / 17,885.53 steps/s | 20.688 us | 16.625 us |

The additional 20-us reverse case measured at least 168.25 us DIR setup.
An isolated development probe passed 20 kHz but failed at 22 kHz; those
experiments are not the released rate setting. The release uses 18 kHz
with margin. Complete output is in `artifacts/speed-simulation.log`; the
[validation report](validation.md) records the associated build and tests.

At fixed 320/320/1280 steps/mm the nominal frequency limit corresponds to
3375/3375/843.75 mm/min. Factory starts are 600/600/300 mm/min with
10 mm/s² acceleration. Valid old 100/100/150 settings remain at those
lower speeds after EEPROM migration. Hardware tuning must follow the
[speed and travel test plan](geschwindigkeit-optimieren.md).

## Historical commissioning evidence

`tests/run-sim.sh` executes the actual ELF on a simulated ATmega2560 with a
DRV8711 register model. It records physical GPIO edges, exact pulse counts,
direction setup and pulse widths, and exercises shutdown and admission guards.
The historical standard-profile 16 MHz simulation recorded a minimum ordinary STEP width of
14.312 us and 4992.81 steps/s over the fastest 64-edge window of a commanded
4992 steps/s Z move under continuous UART status traffic. Forward/reverse XYZ
moves produced the exact expected 64/64/32 pulses. Deterministic FAULTn
injections at both STEP ISR entry points canceled the pending edge and retained
driver alarm 12 before the safety poll could run. These are simulator results.
Neither the old nor the new simulator can establish signal integrity,
real driver current, motor direction,
switch wiring or timing under the real electrical load. Use the hardware test
procedure and an oscilloscope before increasing feed rates or enabling homing.
