#ifndef MM3_LAUNCHER_H
#define MM3_LAUNCHER_H

/* Before runtime initialization: 1 = play/automated launch, 0 = cancelled.
 * MM3_LAUNCHER=1 forces the launcher; =0 skips it. Scripts supplying
 * RECOMP_PB_EXEC skip the UI unless forced. Preferences still load, with
 * explicit environment taking precedence. MM3_LAUNCHER_SMOKE=1 creates and
 * checks the real controls, then cancels without saving or starting the game. */
int mm3_launcher_run(void);

#endif
