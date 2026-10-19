/* Original RF2000v2 display/keypad, isolated Y commissioning UI. GPL-3.0+. */
#ifndef RF2000_PANEL_H
#define RF2000_PANEL_H
#include <stdint.h>

#if RF2000_LOCAL_PANEL
void rf2000_panel_init(void);
void rf2000_panel_reset(void);
void rf2000_panel_tick_isr(void);
uint8_t rf2000_panel_stop_active(void);
void rf2000_panel_service(void);
void rf2000_panel_dispatch(uint8_t command_boundary);
void rf2000_panel_note_alarm(uint8_t alarm);
void rf2000_panel_homing_phase(uint8_t phase);
void rf2000_panel_homing_complete(uint8_t axes);
#if RF2000_CNC
void rf2000_panel_homing_begin(uint8_t axes);
#else
#define rf2000_panel_homing_begin(axes) ((void)0)
#endif
#else
#define rf2000_panel_init() ((void)0)
#define rf2000_panel_reset() ((void)0)
#define rf2000_panel_tick_isr() ((void)0)
#define rf2000_panel_service() ((void)0)
#define rf2000_panel_dispatch(boundary) ((void)0)
#define rf2000_panel_note_alarm(alarm) ((void)0)
#define rf2000_panel_homing_phase(phase) ((void)0)
#define rf2000_panel_homing_complete(axes) ((void)0)
#define rf2000_panel_homing_begin(axes) ((void)0)
#endif
#endif
