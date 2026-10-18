# RF2000 STEP/DIR timing

The standard test profile uses three logical Grbl axes. Five physical DRV8711 channels
are initialized by the driver module; the two reserve channels have no planner
axis and remain asleep throughout motion. Initialization temporarily wakes
their digital circuitry with CTRL.ENBL=0 to perform RESET and clear status;
their motor bridges remain disabled. The STEP implementation is guarded by
`CPU_MAP_2560_RENKFORCE_CTC_V3` and `N_AXIS == 3`.

The separate `rf2000_y_homing_test` profile further masks physical STEP output
to PF6 and limits driver enable to Y. It uses the same pulse timers and minimum
timing constraints; X/Z/A/B motor bridges remain disabled.

All XYZ STEP pins share PORTF (PF0/PF6/PF3). X/Y direction pins PF1/PF7 and Y
SLEEPn PF2 share the same register. Each pulse edge therefore uses one masked
PORTF write. The upstream delayed-pulse path caches complete port values per
axis; applying that path to this board would overwrite other axis STEP bits,
direction bits or the enable signal. The RF2000 path caches only STEP bits and
combines them with the **current** PORTF value inside the interrupt.

X/Y direction bits are written together; Z direction uses PL1. These writes
occur with interrupts disabled, before Timer0 is started. Every other RF2000
module that writes shared ports uses short atomic GPIO operations as well.
Timer1 reenables interrupts only after the port writes and pulse timer setup,
so Timer0 cannot interrupt the read/modify/write sequence.

At the original 16 MHz clock, Timer0 runs at 2 MHz (0.5 us ticks):

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

The test profile accepts `$0` from 10 to 20 us and independently clamps that
range in the pulse code. `$2=0` and `$4=0` are required: STEP is active HIGH,
and the physical nSLEEP polarity is fixed by the board. The planner caps
physical step rates at 5000 Hz; the existing AMASS scheduler may execute its
Timer1 interrupt up to 16000 times per second. This is a conservative software
profile, not a measured hardware maximum. If Timer1 reaches a new output
update before Timer0 completes the previous pulse, motion stops with alarm 3
instead of truncating a pulse or changing direction during it.

Driver wake-up waits the original 25 ms in foreground context before Timer1
is enabled. Neither timer ISR performs SPI or wakes a driver. A fault or reset
cancels pending pulse edges, sleeps all five drivers and stops the pulse timer.
Emergency stopping can intentionally shorten a pulse already in progress;
minimum-width guarantees apply to ordinary motion, not emergency shutdown.

`tests/run-sim.sh` executes the actual ELF on a simulated ATmega2560 with a
DRV8711 register model. It records physical GPIO edges, exact pulse counts,
direction setup and pulse widths, and exercises shutdown and admission guards.
The initial 16 MHz simulation recorded a minimum ordinary STEP width of
14.312 us and 4992.81 steps/s over the fastest 64-edge window of a commanded
4992 steps/s Z move under continuous UART status traffic. Forward/reverse XYZ
moves produced the exact expected 64/64/32 pulses. Deterministic FAULTn
injections at both STEP ISR entry points canceled the pending edge and retained
driver alarm 12 before the safety poll could run. These are simulator results.
It cannot establish signal integrity, real driver current, motor direction,
switch wiring or timing under the real electrical load. Use the hardware test
procedure and an oscilloscope before increasing feed rates or enabling homing.
