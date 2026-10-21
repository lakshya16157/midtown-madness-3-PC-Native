#ifndef MM3_DIRECT_LOG_H
#define MM3_DIRECT_LOG_H

/* Give launches that bypass scripts/play.ps1 (a Steam shortcut, or opening
 * the exe directly) a game log and light frame timing in logs\direct-*.
 * Call first in main, once the working directory holds game_files. Does
 * nothing when stderr is already redirected or MM3_DIRECT_LOG=0. */
void mm3_direct_launch_log(void);

#endif
