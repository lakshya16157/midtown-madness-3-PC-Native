#ifndef MM3_DIRECT_LOG_H
#define MM3_DIRECT_LOG_H

/* Give launches that bypass scripts/play.ps1 (a Steam shortcut, or opening
 * the exe directly) a game log and light frame timing in logs\direct-*.
 * Call first in main, once the working directory holds game_files. Does
 * nothing when stderr is already redirected or MM3_DIRECT_LOG=0. */
void mm3_direct_launch_log(void);

#ifdef _WIN32
/* mm3_recomp.exe is a Windows program with no console of its own (CMakeLists.txt
 * WIN32_EXECUTABLE), so a start from Steam or Explorer opens no console window (issue #47).
 * Call first in main: redirected output (play.ps1, the start script) keeps its handles; started
 * from a shell, the output goes to that shell's console; otherwise to NUL until
 * mm3_direct_launch_log gives stderr its log file. */
void mm3_attach_streams(void);
#endif

/* Log the power source ([MM3_POWER]: mains or battery, battery saver) now
 * and whenever it changes, checked every 30 s. */
void mm3_power_watch(void);

#endif
