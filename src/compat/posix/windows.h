/* Linux/macOS stand-in for <windows.h> in the MM3 port's own sources.
 * The xboxrecomp runtime already implements the Win32 host primitives it needs
 * on POSIX (tools/xboxrecomp/src/platform/win32_compat.h); this header points
 * the port's files at that layer, plus the few extras they use (mm3_win32_extra.h).
 * Only on the include path off Windows (CMakeLists.txt). */
#pragma once
#include "platform/win32_compat.h"
#include "mm3_win32_extra.h"
