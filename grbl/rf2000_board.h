/* RF2000v2 / I851 CTC V3 board safety interface. GPL-3.0-or-later. */
#ifndef RF2000_BOARD_H
#define RF2000_BOARD_H

// Call before any motor, timer, peripheral or interrupt initialization.
void rf2000_board_init(void);

// Force every known heater, mains relay, fan and light output OFF. This may
// also be called from interrupt context; unrelated motor/serial bits survive.
void rf2000_board_outputs_off(void);

#endif
