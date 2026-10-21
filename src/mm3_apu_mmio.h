#ifndef MM3_APU_MMIO_H
#define MM3_APU_MMIO_H
#include <stdint.h>

/* Exact 32-bit APU accesses for checked, constant guest MMIO sites. The same
 * register handlers and voice locks as the exception path remain authoritative. */
uint32_t mm3_apu_mmio_read32(uint32_t address);
void mm3_apu_mmio_write32(uint32_t address, uint32_t value);

/* Diagnostic comparison control. Switching paths retains the same device
 * handlers and in-flight voice state; it never resets or flushes audio. */
int mm3_apu_mmio_set_enabled(int enabled);
int mm3_apu_mmio_get_enabled(void);

#endif
