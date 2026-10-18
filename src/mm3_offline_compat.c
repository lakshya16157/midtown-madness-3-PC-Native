#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

void sub_0019531D_original(void);

/* Offline compatibility: MM3's network object's readiness method
 * masks bit 0x20 from XNetGetTitleXnAddr and returns whether any other bit is
 * set. The initialization method at 0x195341 polls this virtual method every
 * 100 ms. With the incomplete guest network stack it never finishes, blocking
 * frontend initialization after the movies. Preserve network initialization
 * and bypass only its readiness predicate when the network adapter is off.
 * System Link uses the original predicate and waits for real XNet readiness.
 * Evidence: post-intro worker stack contains 0x1953A0 and repeatedly calls
 * 0x19531D while no further frontend resources are opened.
 */
void sub_0019531D(void)
{
    static int reported;
    const char *network = getenv("RECOMP_NET");
    if (network && *network && strcmp(network, "0")) {
        sub_0019531D_original();
        return;
    }
    if (!reported) {
        reported = 1;
        fprintf(stderr, "[MM3_OFFLINE] network readiness forced to 1\n");
    }
    g_eax = 1;
    g_esp += 4; /* original cdecl ret, no arguments */
}
