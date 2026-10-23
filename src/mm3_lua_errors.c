#include <stdio.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"

/* The game's Lua _ERRORMESSAGE (sub_0020F684, lua_State in ECX) only keeps
 * the message in an internal list. Print it as well, so a race script that
 * stops loading says why. The message is the handler's first argument, read
 * with the game's own lua_isstring (sub_000882ED) and lua_tostring
 * (sub_0008832A), which take the state in ECX and the index in EDX.
 *
 * Race scripts also report through it: a message starting "circuit: "
 * (Scripts/logic_circuit.lua's circuitLog) is a log line, printed as
 * "[MM3_CIRCUIT] <seconds since the first one> <message>" and kept out of the
 * error list. Circuits log every car's position once a second
 * (tools/races/trace.py reads them). */
extern void sub_0020F684_original(void);
extern void sub_000882ED(void);
extern void sub_0008832A(void);

#define LUA_ERROR_REPORTS 64u
#define CIRCUIT_LOG_LINES 8000u
#define CIRCUIT_PREFIX "circuit: "

void sub_0020F684(void)
{
    static unsigned errors, lines;
    static clock_t first_line;
    uint32_t state = g_ecx, text = 0;
    char message[512];
    int circuit = 0;
    {
        uint32_t a = g_eax, c = g_ecx, d = g_edx, b = g_ebx, sp = g_esp;
        uint32_t si = g_esi, di = g_edi, bp = g_ebp, seh = g_seh_ebp;
        double fp[8];
        int top = g_fp_top;
        memcpy(fp, g_fp_stack, sizeof fp);
        g_ecx = state;
        g_edx = 1u;
        PUSH32(g_esp, 0x0020F695u); /* the handler's own call sites */
        sub_000882ED();
        if (g_eax) {
            g_ecx = state;
            g_edx = 1u;
            PUSH32(g_esp, 0x0020F6A3u);
            sub_0008832A();
            text = g_eax;
        }
        message[0] = 0;
        if (text >= 0x10000u && text < 0x08000000u - sizeof message) {
            size_t i;
            for (i = 0; i + 1 < sizeof message && (message[i] = (char)MEM8(text + (uint32_t)i)) != 0; ++i) {}
            message[i] = 0;
        }
        g_eax = a; g_ecx = c; g_edx = d; g_ebx = b; g_esp = sp;
        g_esi = si; g_edi = di; g_ebp = bp; g_seh_ebp = seh;
        memcpy(g_fp_stack, fp, sizeof fp);
        g_fp_top = top;
    }
    circuit = !strncmp(message, CIRCUIT_PREFIX, sizeof CIRCUIT_PREFIX - 1);
    if (circuit) {
        if (lines < CIRCUIT_LOG_LINES) {
            clock_t now = clock();
            if (!lines) first_line = now;
            fprintf(stderr, "[MM3_CIRCUIT] %.2f %s\n", (double)(now - first_line) / CLOCKS_PER_SEC,
                    message + sizeof CIRCUIT_PREFIX - 1);
            ++lines;
        }
        g_eax = 0;       /* no results */
        g_esp += 4u;     /* the handler's RET */
        return;
    }
    if (message[0] && errors < LUA_ERROR_REPORTS) {
        fprintf(stderr, "[MM3_LUA] %s\n", message);
        ++errors;
    }
    sub_0020F684_original();
}
