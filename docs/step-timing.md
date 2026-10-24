# RF2000 STEP/DIR timing

All RF2000 profiles use three logical Grbl axes and initialize five physical
DRV8711 channels. A/B have no planner axis and remain disabled. Their digital
circuits are temporarily awake during initial RESET/readback with CTRL.ENBL=0;
their motor bridges remain disabled. Pins and current settings are unchanged.

The current CNC profile is **`20261024-rf2000-cnc-adaptive-spi`**. It retains a
fixed virtual 1/256 coordinate grid, nominal **20480 units/mm on XYZ**, while
physical resolution changes between **1/256, 1/128, 1/64, 1/32, 1/16 and 1/8**.
Axis and path feed are capped at **1000 mm/min**. The physical pulse budget is
12 kHz. These are configuration/output limits, not a mechanical qualification.
The separate standard/Y commissioning profiles retain their former fixed
resolutions and 5-kHz limit.

## Shared GPIO and pulse constraints

All XYZ STEP pins share PORTF: PF0/PF6/PF3. X/Y DIR uses PF1/PF7, and Y SLEEPn
shares PF2. A pulse edge uses one masked write of the current PORTF value; the
firmware never replays a cached whole-port image. Z DIR uses PL1, shared with
several SCS signals. Shared-port changes use short atomic operations.

Timer0 runs at 2 MHz, or 0.5-us ticks, at the original 16-MHz clock. `$0`
accepts 10–20 us. STEP is active HIGH; `$2=0` and `$4=0` are hardware-fixed.
The first physical rising edge after a DIR change uses the Timer0 Compare A
setup delay of at least 5 us. A tick without a physical pulse cannot consume
that requirement. Once DIR is stable, a physical rising edge can be written
from Timer1; Timer0 measures the width from the actual edge and clears STEP.
Interrupt latency lengthens these minimum intervals.

A new output update must not overlap an unfinished pulse or change DIR while
STEP is HIGH. Such an inconsistency stops motion instead of silently
truncating ordinary pulses. A fault/reset shutdown can intentionally shorten
an active pulse; ordinary minimum-width guarantees do not apply to emergency
shutdown. SPI and the 25-ms driver wake delay do not run inside STEP ISRs.

## Adaptive segment execution

The planner retains virtual fine-grid positions. Foreground segment
preparation converts the desired absolute endpoints into physical pulse
counts appropriate to the selected resolution. It carries a future ideal
position and predicted physical position separately, rather than using the
current live position for queued work. An ISR distributes those prepared
physical pulses across the segment. It does not iterate over every virtual
1/256 position at high feed.

The driver electrical phase is independent of GRBL coordinates. TI
§7.3.3 specifies that, after MODE changes, the next STEP advances to the next
valid state of that new mode. That first increment can be shorter than a
normal new-mode increment. Position accounting uses the actual phase-aware
increment of each emitted pulse. Suppressed homing pulses cannot advance the
hardware phase or reported physical position.

The normal quantizer carries remaining distance in the planned direction;
MODE refinement does not create artificial reverse steps. With nominal
mechanics, the carried following distance is at most 31 fine-grid units,
about **1.514 um per axis**, below one 1/8 step. This is a digital trajectory
bound, not an achieved machining tolerance. Rest distance is resolved at a
zero-speed junction, feed hold or final stop. Continuous CAM lines and arcs
carry it across block boundaries rather than stopping after every line.
Fine completion may add up to 31 pulses per axis at a bounded rate around
6 kHz, about 5.2 ms before other processing delays. Fault/reset discards this
completion; it must not trigger corrective motion after an alarm.

A common XYZ resolution is selected from the highest required axis rate in
the actual prepared segment. The selection coarsens above a 12-kHz physical
requirement and refines only when the finer mode would stay at or below
9 kHz. Hysteresis avoids chatter. Initial nominal stage boundaries are about
35.16/70.31/140.63/281.25/562.5 mm/min; the final 1/8 range ends at the
1000-mm/min feed cap. At 1000 mm/min its nominal pulse rate is 10,666.7 Hz.

Homing uses one fixed profile through each phase, normally 1/32 at the
100/20-mm/min defaults. If the configured seek/locate rates require it,
the phase profile is coarsened to 1/16 or 1/8 before the phase starts.
This avoids changing the predicted phase of a motor already stopped by its
homing switch. `$LR` at F20 uses fine 1/256 resolution.

## MODE transfer barrier

A requested MODE change waits until no STEP pulse is pending. Timer1 is
paused before foreground SPI writes and reads back the XYZ CTRL fields.
The matching segment metadata becomes active only after verified register
values and another fault/reset check. MODE setup/hold relative to STEP must
meet TI §6.7. A mismatched readback latches a driver fault and disables all
five channels; it does not continue with an assumed resolution.

Other interrupts remain available during transfers, apart from short atomic
GPIO operations. Timer2 limit polling, UART and INT6 fault latching therefore
remain active. MODE writes use the fast transport, but CTRL readback uses
the slower startup transport to allow the open-drain SDATO line to rise.
Both run in foreground only; neither transaction enters a STEP ISR.
The synthetic delayed-HIGH regression reproduces the former Alarm12
with the old fast read and passes with the settled read path.

Transfer/readback and fine completion add time. High rates and exact command
endpoints do not imply an identical wall-clock profile at every short segment.
The real output rate, pause duration, pulse widths, phase-weighted travel and
failure paths must be assessed together. `$MS` reads the current cached mode
without SPI or motion; the LCD continues to identify the current operation.

## Validation and hardware

The current build, exact ELF hash, executed test commands and measured
limits are recorded in [validation.md](validation.md). Historical 18-kHz
measurements from the fixed quarter-step CNC profile are not evidence for
the adaptive implementation. Tests must decode MODE and the TI indexer
semantics, not merely count unweighted STEP edges.

Simulation can verify the actual AVR code, weighted GPIO displacement,
phase continuity, fault guards, and retained positions. It cannot establish
signal integrity, current, mechanical direction, real step loss or machining
accuracy. Follow [hardware-test.md](hardware-test.md) and
[adaptive-mikroschritte.md](adaptive-mikroschritte.md).
