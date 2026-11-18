#ifndef MM3_LAUNCHER_H
#define MM3_LAUNCHER_H

/* Before runtime initialization: 1 = play/automated launch, 0 = cancelled.
 * --settings and --play override MM3_LAUNCHER. Set MM3_LAUNCHER to 1 to show
 * the window, 0 to skip it, or auto to use the saved startup preference.
 * Scripts supplying only RECOMP_PB_EXEC skip the UI. The game still loads its
 * preferences. MM3_LAUNCHER_SMOKE=1 checks the real controls, then cancels. */
int mm3_launcher_run(int argc, char *const argv[]);

#endif
