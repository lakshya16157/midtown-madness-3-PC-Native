"""Exercise the production producer observer and unchanged native kick ABI.

Run after scripts/enter-env.ps1. Clocks and read snapshots are deterministic;
the fixture includes the real observer and extracts the real kick function.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
#include "kernel.h"
#include "mm3_recomp_types.h"
static struct {unsigned env, frequency, qpc, cycles, thread, id, reads, geterror, seterror, log;} calls;
static const char *fake_setting;
static LONGLONG fake_qpc, fake_frequency=1000000;
static ULONG64 fake_cycles;
static int qpc_ok=1, frequency_ok=1, cycles_ok=1;
static XBOX_FILE_READ_STATS fake_reads;
static char logs[20000];
static size_t log_size;
static void dirty(void){errno=ERANGE;SetLastError(9876);}
static char *test_env(const char *name){++calls.env;assert(!strcmp(name,"RECOMP_PRODUCER_TRACE"));dirty();return (char *)fake_setting;}
static BOOL test_frequency(LARGE_INTEGER *value){++calls.frequency;value->QuadPart=fake_frequency;dirty();return frequency_ok;}
static BOOL test_qpc(LARGE_INTEGER *value){++calls.qpc;value->QuadPart=fake_qpc;dirty();return qpc_ok;}
static BOOL test_cycles(HANDLE thread,PULONG64 value){++calls.cycles;assert(thread==(HANDLE)(intptr_t)-2);*value=fake_cycles;dirty();return cycles_ok;}
static HANDLE test_thread(void){++calls.thread;dirty();return (HANDLE)(intptr_t)-2;}
static DWORD test_id(void){++calls.id;dirty();return 4321;}
static DWORD test_geterror(void){++calls.geterror;return GetLastError();}
static void test_seterror(DWORD error){++calls.seterror;SetLastError(error);}
void xbox_GetFileReadStats(XBOX_FILE_READ_STATS *value){++calls.reads;*value=fake_reads;dirty();}
static void test_log(const char *format,...){
 va_list args;int written;++calls.log;va_start(args,format);
 written=vsnprintf(logs+log_size,sizeof logs-log_size,format,args);va_end(args);
 assert(written>=0 && (size_t)written<sizeof logs-log_size);log_size+=(size_t)written;dirty();
}
#define getenv test_env
#define QueryPerformanceFrequency test_frequency
#define QueryPerformanceCounter test_qpc
#define QueryThreadCycleTime test_cycles
#define GetCurrentThread test_thread
#define GetCurrentThreadId test_id
#define GetLastError test_geterror
#define SetLastError test_seterror
#define MM3_PRODUCER_LOG test_log
#include "mm3_producer_diagnostics.c"
#undef GetLastError
#undef SetLastError

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
static void xbox_Nv2aWbcWait(uint32_t address){(void)address;assert(!"Unexpected lag in observer fixture");}
/* NATIVE_KICK */

static void check_error(void){assert(errno==EDOM && GetLastError()==1234);}
static void kick(void){errno=EDOM;SetLastError(1234);mm3_producer_diagnostics_kick();check_error();}
static void reset(const char *value){
 fake_setting=value;s_producer_initialized=0;s_producer_enabled=0;s_producer_frequency=0;s_producer_gap_ticks=0;
 memset(&s_producer_previous,0,sizeof s_producer_previous);memset(&calls,0,sizeof calls);
 memset(&fake_reads,0,sizeof fake_reads);log_size=0;logs[0]=0;
 fake_qpc=1000000;fake_cycles=100;fake_frequency=1000000;qpc_ok=frequency_ok=cycles_ok=1;
 errno=EDOM;SetLastError(1234);mm3_producer_diagnostics_init();check_error();
}
static void disabled(void){
 const char *values[]={NULL,"","0","2","true","01"};unsigned i;
 for(i=0;i<sizeof values/sizeof *values;i++){
  unsigned before;reset(values[i]);assert(!s_producer_enabled && calls.env==1 && !calls.frequency);
  before=calls.geterror+calls.seterror+calls.env;
  for(unsigned j=0;j<10000;j++)kick();
  assert(calls.geterror+calls.seterror+calls.env==before);
  assert(!calls.qpc && !calls.cycles && !calls.thread && !calls.id && !calls.reads && !calls.log);
 }
 reset("1");assert(s_producer_enabled && calls.frequency==1);
 mm3_producer_diagnostics_init();assert(calls.env==1 && calls.frequency==1);
 reset("1");s_producer_enabled=0;s_producer_initialized=0;frequency_ok=0;
 mm3_producer_diagnostics_init();kick();assert(!calls.qpc && !s_producer_enabled);
 reset("1");s_producer_enabled=0;s_producer_initialized=0;fake_frequency=0;
 mm3_producer_diagnostics_init();kick();assert(!calls.qpc && !s_producer_enabled);
}
static void intervals(void){
 reset("1");fake_reads=(XBOX_FILE_READ_STATS){10,5000,1000000};kick();assert(!calls.log);
 fake_qpc+=39999;fake_cycles+=100;fake_reads.read_count+=1;fake_reads.read_qpc_ticks+=1000;
 kick();assert(!calls.log);
 fake_qpc+=40000;fake_cycles+=200;fake_reads.read_count+=3;fake_reads.read_qpc_ticks+=12000;
 kick();assert(calls.log==1 && calls.id==1);
 assert(strstr(logs,"qpc_ms=1079.999 start_qpc_ms=1039.999 duration_ms=40.000 thread=4321"));
 assert(strstr(logs,"cpu_cycle_delta=200 cpu_cycles_valid=1 read_count_delta=3 read_wall_ticks_delta=12000 read_wall_ms=12.000 read_stats_valid=1"));
 fake_qpc+=1;kick();assert(calls.log==1); /* Every kick advances the baseline. */
 reset("1");s_producer_frequency=1001;s_producer_gap_ticks=41;kick();fake_qpc+=40;kick();assert(!calls.log);
 fake_qpc+=41;kick();assert(calls.log==1); /* The integer threshold rounds up. */
 reset("1");fake_reads=(XBOX_FILE_READ_STATS){UINT64_MAX-1,UINT64_MAX-999,1000000};kick();
 fake_qpc+=50000;fake_cycles+=50;fake_reads.read_count=1;fake_reads.read_qpc_ticks=2000;kick();
 assert(strstr(logs,"read_count_delta=3 read_wall_ticks_delta=3000 read_wall_ms=3.000 read_stats_valid=1"));
}
static void invalid(void){
 reset("1");kick();qpc_ok=0;fake_qpc+=100000;kick();assert(!calls.log && !s_producer_previous.valid);
 qpc_ok=1;fake_qpc+=100000;kick();assert(!calls.log);fake_qpc+=40000;kick();assert(calls.log==1);
 reset("1");kick();fake_qpc=-1;kick();assert(!s_producer_previous.valid && !calls.log);
 fake_qpc=1000000;kick();fake_qpc=999999;kick();assert(!calls.log);
 fake_qpc+=40000;kick();assert(calls.log==1); /* Rollback rebases without unsigned overflow. */
 reset("1");kick();fake_qpc+=40000;cycles_ok=0;kick();
 assert(strstr(logs,"cpu_cycle_delta=0 cpu_cycles_valid=0"));
 fake_qpc+=40000;cycles_ok=1;fake_cycles=1000;kick();
 assert(calls.log==2 && strstr(logs,"read_stats_valid=0")); /* Unmeasured reads are unknown. */
 reset("1");fake_cycles=UINT64_MAX-5;kick();fake_qpc+=40000;fake_cycles=5;kick();
 assert(strstr(logs,"cpu_cycle_delta=0 cpu_cycles_valid=0"));
 reset("1");fake_reads.qpc_frequency=1000000;kick();fake_qpc+=40000;fake_reads.qpc_frequency=10;kick();
 assert(strstr(logs,"read_count_delta=0 read_wall_ticks_delta=0 read_wall_ms=0.000 read_stats_valid=0"));
}
static DWORD WINAPI other_thread(void *unused){
 (void)unused;assert(!s_producer_previous.valid);kick();assert(s_producer_previous.valid);return 0;
}
static void floating_point(void){
 fenv_t original,before,after;unsigned csr;
 assert(fegetenv(&original)==0);reset("1");
 assert(fesetround(FE_DOWNWARD)==0);assert(feraiseexcept(FE_DIVBYZERO|FE_INEXACT)==0);
 memset(&before,0,sizeof before);assert(fegetenv(&before)==0);csr=_mm_getcsr();
 kick();memset(&after,0,sizeof after);assert(fegetenv(&after)==0);
 assert(!memcmp(&before,&after,sizeof before) && _mm_getcsr()==csr);
 fake_qpc+=1;kick();memset(&after,0,sizeof after);assert(fegetenv(&after)==0);
 assert(!memcmp(&before,&after,sizeof before) && _mm_getcsr()==csr);
 fake_qpc+=50000;kick();memset(&after,0,sizeof after);assert(fegetenv(&after)==0);
 assert(!memcmp(&before,&after,sizeof before) && _mm_getcsr()==csr);
 assert(fesetenv(&original)==0);
}
static void abi(void){
 unsigned char *ram=(unsigned char *)calloc(1,0x400000);double fp[8];HANDLE worker;
 assert(ram);g_xbox_mem_offset=(ptrdiff_t)ram;MEM32(0x351F48)=0x10000;
 MEM32(0x10000+0x17C8)=0x20000;MEM32(0x20000+0x100410)=0xABCCEF00;
 reset("1");fake_qpc=2000000;fake_reads.qpc_frequency=1000000;kick();
 worker=CreateThread(NULL,0,other_thread,NULL,0,NULL);assert(worker);
 assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
 assert(s_producer_previous.qpc==2000000 && calls.log==0); /* Baselines are TLS. */
 g_eax=1;g_ecx=2;g_edx=3;g_esp=0x300000;g_ebx=4;g_esi=5;g_edi=6;g_ebp=7;
 g_seh_ebp=8;g_fs_base=9;g_eflags=10;g_fp_top=3;g_df=1;g_fp_cmp=2;
 g_fp_control_word=0x027F;g_fp_cc=0x4500;
 for(unsigned i=0;i<8;i++)g_fp_stack[i]=fp[i]=100.25+i;
 fake_qpc+=50000;fake_cycles+=100;errno=EDOM;SetLastError(1234);sub_00344AB0();check_error();
 assert(g_eax==0x20000 && g_ecx==0xABCDEF00 && g_esp==0x300004);
 assert(g_edx==3 && g_ebx==4 && g_esi==5 && g_edi==6 && g_ebp==7);
 assert(g_seh_ebp==8 && g_fs_base==9 && g_eflags==10);
 assert(g_fp_top==3 && g_df==1 && g_fp_cmp==2 && g_fp_control_word==0x027F && g_fp_cc==0x4500);
 assert(!memcmp(fp,g_fp_stack,sizeof fp));assert(MEM32(0x20000+0x100410)==0xABCCEF00);
 free(ram);
}
int main(void){disabled();intervals();invalid();floating_point();abi();puts("PASS: producer diagnostics disabled zero API calls, exact 40ms threshold, deltas/wrap/errors, host FP environment, TLS and native kick ABI");return 0;}
'''


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    manual = (ROOT / 'src/recomp_manual.c').read_text()
    kick = re.search(r'^void sub_00344AB0\(void\)\n\{.*?^\}', manual, re.M | re.S).group()
    assert kick.count('mm3_producer_diagnostics_kick();') == 1
    assert kick.index('mm3_producer_diagnostics_kick();') < kick.index('_mm_sfence();')
    with tempfile.TemporaryDirectory(prefix='mm3-producer-diagnostics-') as path:
        folder = Path(path)
        source, executable = folder / 'contract.c', folder / 'contract.exe'
        source.write_text(SOURCE.replace('/* NATIVE_KICK */', kick))
        for optimization in ('/Od', '/O2'):
            subprocess.run(['cl', '/nologo', '/W4', optimization, '/std:c11',
                            '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'src'),
                            '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                            '/I' + str(ROOT / 'tools/xboxrecomp/src'),
                            '/I' + str(ROOT / 'tools/xboxrecomp/src/kernel'),
                            '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(executable),
                            str(source)], check=True, cwd=folder)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
