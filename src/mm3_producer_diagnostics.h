#ifndef MM3_PRODUCER_DIAGNOSTICS_H
#define MM3_PRODUCER_DIAGNOSTICS_H

/* Configure before guest workers start. Disabled kicks perform no host API
 * calls. Enabled diagnostics observe the interval between this thread's D3D
 * push-buffer kicks; they never change the guest fence or publication order. */
void mm3_producer_diagnostics_init(void);
void mm3_producer_diagnostics_kick(void);

#endif
