/* Exercise the production wrapper's guest return/stack contract. */
#include "../src/mm3_offline_compat.c"
#include <assert.h>
RECOMP_TLS uint32_t g_eax, g_esp;
static unsigned original_calls;
void sub_0019531D_original(void)
{
    ++original_calls;
    g_eax = 42;
    g_esp += 4;
}
int main(void)
{
    const char *mode[] = {"", "0", "nat", "1"};
    for (unsigned i = 0; i < sizeof mode / sizeof mode[0]; ++i) {
        assert(!_putenv_s("RECOMP_NET", mode[i]));
        original_calls = 0; g_eax = 0xCDCDCDCD; g_esp = 0x10000;
        sub_0019531D();
        assert(g_esp == 0x10004); /* exactly one guest return in either path */
        assert(original_calls == (i >= 2 ? 1u : 0u));
        assert(g_eax == (i >= 2 ? 42u : 1u));
    }
    puts("Offline readiness: unset/0 bypass; nat/1 original; guest stack preserved");
    return 0;
}
