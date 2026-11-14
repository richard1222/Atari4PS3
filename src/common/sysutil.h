#ifndef A4_SYSUTIL_H
#define A4_SYSUTIL_H

/* System-event handling (PS button).
 *
 * The PS button's "Quit Game" request arrives as SYSUTIL_EXIT_GAME
 * through the registered sysutil callback — and only there. With no
 * callback registered and no sysUtilCheckCallback() in the loop,
 * pressing PS does nothing at all. Pattern follows SalsaDS and Neon
 * Lounge (both proven on this console): register once at startup,
 * pump once per frame, latch the exit request, quit cleanly. */

int a4_sysutil_init(void); /* returns the register result (0 = ok) */
int a4_sysutil_poll(void); /* pump events; 1 once exit was requested */
int a4_sysutil_exit_requested(void);

#endif
