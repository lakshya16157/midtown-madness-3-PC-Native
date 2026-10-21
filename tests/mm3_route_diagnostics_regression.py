"""Exercise the real route observer and exact generated-source seam anchors."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ('sub_0013AB27', 'sub_0024FA01', 'sub_0024F917', 'sub_0024F964')

HARNESS = r'''
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
#include <xmmintrin.h>
#include "mm3_recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x12000,g_xbox_code_hi=0x400000;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static const char *setting;
static LONG reads;
static unsigned environment_calls, log_calls;
static char logs[1048576];
static size_t log_size;
static void dirty(void){errno=ERANGE;SetLastError(9876);}
static DWORD test_environment(LPCSTR name,LPSTR result,DWORD size){
 assert(!strcmp(name,"MM3_ROUTE_DIAGNOSTICS"));++environment_calls;dirty();
 if(!setting)return 0;size_t n=strlen(setting);if(n+1>size)return (DWORD)n+1;
 memcpy(result,setting,n+1);return (DWORD)n;
}
static BOOL test_read(HANDLE process,LPCVOID address,LPVOID result,SIZE_T size,SIZE_T *done){
 InterlockedIncrement(&reads);BOOL ok=ReadProcessMemory(process,address,result,size,done);dirty();return ok;
}
static void test_log(const char *format,...){
 va_list args;int n;va_start(args,format);++log_calls;
 n=vsnprintf(logs+log_size,sizeof logs-log_size,format,args);va_end(args);
 assert(n>=0&&(size_t)n<sizeof logs-log_size);log_size+=(size_t)n;
 dirty();fesetround(FE_DOWNWARD);feraiseexcept(FE_DIVBYZERO);_mm_setcsr(0x1F80);
}
#define GetEnvironmentVariableA test_environment
#define ReadProcessMemory test_read
#define MM3_ROUTE_LOG test_log
#include "mm3_route_diagnostics.c"
#undef ReadProcessMemory

typedef struct GuestState {
 uint32_t regs[11];int top,df,cmp;uint16_t control,cc;double fp[8];RecompXmm xmm[8];
} GuestState;
static GuestState guest_state(void){
 GuestState s={0};uint32_t r[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
 memcpy(s.regs,r,sizeof r);s.top=g_fp_top;s.df=g_df;s.cmp=g_fp_cmp;s.control=g_fp_control_word;s.cc=g_fp_cc;
 memcpy(s.fp,g_fp_stack,sizeof s.fp);RecompXmm x[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
 memcpy(s.xmm,x,sizeof x);return s;
}
static unsigned char *ram,*copy;
static void reset(const char *value){
 setting=value;route_enabled=-1;route_sequence=enqueue_position=event_position=baseline_reports=bad_reports=duplicate_reports=0;
 memset(route_enqueues,0,sizeof route_enqueues);memset(route_events,0,sizeof route_events);
 reads=0;environment_calls=log_calls=0;log_size=0;logs[0]=0;
 memset(ram,0,0x800000);
 MEM32(0x10024)=0x12000;MEM32(0x10020)=2;MEM32(0x1001C)=0;
 MEM32(0x1100C)=0x17000;MEM32(0x11040)=0x12000;
 MEM32(0x12000)=0x13000;MEM32(0x13004)=0x16000;
 MEM32(0x14000)=0x14040;MEM32(0x14004)=0x24FAF9;
 MEM32(0x14040)=0x14080;MEM32(0x14044)=0x143AAC;
 MEM32(0x14080)=0;MEM32(0x14084)=0x1522C2;
 MEM32(0x1500C)=0x24F87A;
}
static void before(fenv_t *fp,unsigned *csr,GuestState *state){
 *state=guest_state();memcpy(copy,ram,0x800000);
 fesetround(FE_UPWARD);feraiseexcept(FE_INEXACT);
 memset(fp,0,sizeof *fp);assert(fegetenv(fp)==0);*csr=_mm_getcsr();errno=EDOM;SetLastError(1234);
}
static void after(const fenv_t *fp,unsigned csr,const GuestState *state){
 fenv_t now;memset(&now,0,sizeof now);assert(fegetenv(&now)==0);
 assert(!memcmp(fp,&now,sizeof now)&&csr==_mm_getcsr());
 assert(errno==EDOM&&GetLastError()==1234);GuestState now_state=guest_state();
 assert(!memcmp(state,&now_state,sizeof *state));assert(!memcmp(ram,copy,0x800000));
}
static void enqueue(void){
 fenv_t fp;unsigned csr;GuestState state;before(&fp,&csr,&state);
 mm3_route_enqueue(0x10000,0x11000,0x12000,0,0x14000);after(&fp,csr,&state);
}
static void consume(void){
 fenv_t fp;unsigned csr;GuestState state;before(&fp,&csr,&state);
 mm3_route_consume(0x11010,0x18000,0x15000);after(&fp,csr,&state);
}
static DWORD WINAPI producer(LPVOID unused){(void)unused;enqueue();return GetCurrentThreadId();}
static void disabled(void){
 const char *values[]={NULL,"","0","01","true"};
 for(unsigned i=0;i<sizeof values/sizeof *values;i++){
  reset(values[i]);enqueue();consume();mm3_route_lifetime(2,0,0,0,0,0,0);
  assert(environment_calls==1&&!reads&&!log_calls&&!route_sequence);
  for(unsigned j=0;j<1000;j++)mm3_route_consume(0xFFFFFFFF,0,0);
  assert(environment_calls==1&&!reads&&!log_calls);
 }
}
static void lifetime(void){
 reset("1");enqueue();assert(route_sequence==1&&enqueue_position==1);
 assert(route_enqueues[0].callers[0]==0x24FAF9&&route_enqueues[0].callers[1]==0x143AAC);
 assert(route_enqueues[0].callers[2]==0x1522C2&&route_enqueues[0].default_graph==0x12000);
 assert(strstr(logs,"producer=00143AAC")&&strstr(logs,"target=00016000"));
 consume();assert(!bad_reports&&route_events[1].callers[0]==0x24F87A);
 for(unsigned event=1;event<=3;event++){
  fenv_t fp;unsigned csr;GuestState state;before(&fp,&csr,&state);
  mm3_route_lifetime(event,0x10000,0x11000,0x17000,0x14000,5,0x10010);after(&fp,csr,&state);
  assert(route_events[event+1].kind==event&&route_events[event+1].iterator_index==5);
 }
 MEM32(0x12000)=0x40000;MEM32(0x40004)=0x4000358;consume();
 assert(bad_reports==1&&strstr(logs,"[ROUTE_BAD]")&&strstr(logs,"[ROUTE_ENQUEUE_MATCH]"));
 assert(strstr(logs,"kind=2 scheduler=00010000")&&strstr(logs,"iterator_index=00000005"));
 unsigned before_logs=log_calls;for(unsigned i=0;i<20;i++)consume();
 assert(bad_reports==8&&log_calls>before_logs);before_logs=log_calls;
 for(unsigned i=0;i<1000;i++)consume();assert(log_calls==before_logs);
 /* A valid changed adapter is also suspicious even if its target is code. */
 reset("1");enqueue();MEM32(0x11040)=0x19000;MEM32(0x19000)=0x13000;consume();assert(bad_reports==1);
 /* Unmapped and malformed input must be observable without access violations. */
 reset("1");mm3_route_consume(0x07FFFFF0,0,0);assert(bad_reports==1);
}
static void cross_thread_and_wrap(void){
 reset("1");HANDLE thread=CreateThread(NULL,0,producer,NULL,0,NULL);assert(thread);
 assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);DWORD id;assert(GetExitCodeThread(thread,&id));CloseHandle(thread);
 assert(route_enqueues[0].thread==id&&id!=GetCurrentThreadId());
 MEM32(0x12000)=0x40000;consume();assert(strstr(logs,"[ROUTE_ENQUEUE_MATCH]"));
 reset("1");for(unsigned i=0;i<ROUTE_RING+20;i++){
  MEM32(0x11040)=0x12000;mm3_route_enqueue(0x10000,0x11000,0x12000,i,0x14000);
 }
 assert(log_calls==8&&baseline_reports==8&&route_sequence==ROUTE_RING+20);
 MEM32(0x12000)=0x40000;consume();assert(bad_reports==1);
 assert(strstr(logs,"[ROUTE_ENQUEUE_MATCH] sequence=276 ")&&strstr(logs,"optional=00000113"));
}
static void duplicate_queue(void){
 reset("1");MEM32(0x10014)=0x1A000;MEM32(0x10018)=4;MEM32(0x1A000)=0x1B000;
 MEM32(0x1B000)=0x11000;MEM32(0x10020)=1;enqueue();
 assert(!duplicate_reports&&route_enqueues[0].queue_node_occurrences==1);
 mm3_route_lifetime(2,0x10000,0x11000,0x17000,0x14000,0,0x10010);
 MEM32(0x1B004)=0x11000;MEM32(0x10020)=2;enqueue();
 assert(duplicate_reports==1&&route_enqueues[1].queue_node_occurrences==2);
 assert(route_enqueues[1].queue_scan_count==2&&route_enqueues[1].prior_enqueue==1);
 assert(strstr(logs,"[ROUTE_DUPLICATE]")&&strstr(logs,"[ROUTE_ENQUEUE_PRIOR]"));
 assert(strstr(logs,"queue_node_occurrences=2")&&strstr(logs,"kind=2 scheduler=00010000"));
 for(unsigned i=0;i<30;i++)enqueue();assert(duplicate_reports==8&&baseline_reports==8);
 unsigned n=log_calls;for(unsigned i=0;i<1000;i++)enqueue();assert(log_calls==n);
}
int main(void){
 ram=calloc(1,0x800000);copy=malloc(0x800000);assert(ram&&copy);g_xbox_mem_offset=(ptrdiff_t)ram;
 g_eax=1;g_ecx=2;g_edx=3;g_esp=4;g_ebx=5;g_esi=6;g_edi=7;g_ebp=8;g_seh_ebp=9;g_fs_base=10;g_eflags=11;
 g_fp_top=3;g_df=1;g_fp_cmp=2;g_fp_control_word=0x27F;g_fp_cc=0x4500;
 for(unsigned i=0;i<8;i++)g_fp_stack[i]=100.25+i;
 disabled();lifetime();cross_thread_and_wrap();duplicate_queue();free(ram);free(copy);
 puts("PASS: route observer default off, read-only guest state, errno/LastError/host FPU, cross-thread producer match, lifetime/iterator history, fixed-ring wrap and bounded logs");return 0;
}
'''


def seams(folder):
    bodies = {}
    for path in (ROOT / 'src/recomp/gen').glob('recomp_*.c'):
        source = path.read_text()
        for name in FUNCTIONS:
            match = re.search(r'void ' + name + r'\(void\)\n\{.*?\n\}', source, re.S)
            if match:
                assert name not in bodies
                bodies[name] = match.group()
    assert len(bodies) == 4
    script = folder / 'seam.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_ai_query_stack_seam.cmake').as_posix()}")
include("{(ROOT / 'scripts/mm3_route_diagnostics_seam.cmake').as_posix()}")
file(READ "${{INPUT}}" source)
if(FUNCTION STREQUAL "sub_0013AB27")
 mm3_fix_ai_query_stack(source)
endif()
mm3_add_route_diagnostics(source ${{FUNCTION}})
file(WRITE "${{OUTPUT}}" "${{source}}")
''')
    inp, out = folder / 'input.c', folder / 'output.c'

    def apply(name, source, success=True):
        inp.write_bytes(source)
        result = subprocess.run(['cmake', '-DFUNCTION=' + name, '-DINPUT=' + str(inp),
            '-DOUTPUT=' + str(out), '-P', str(script)], capture_output=True, text=True)
        assert (result.returncode == 0) == success, (name, result.stderr)
        return out.read_text() if success else ''

    for name, body in bodies.items():
        for ending in ('\n', '\r\n'):
            fixed = apply(name, body.replace('\n', ending).encode())
            assert fixed.count('#include "mm3_route_diagnostics.h"') == 1
            assert fixed.count('mm3_route_') == (3 if name == 'sub_0024F964' else 2)
            stripped = re.sub(r'^#include "mm3_route_diagnostics.h"\n|^    mm3_route_.*;\n', '', fixed, flags=re.M)
            if name == 'sub_0013AB27':
                remove_marker = lambda value: value.replace('    { uint32_t _icall_esp = g_esp;\n', '')
                assert remove_marker(stripped) == remove_marker(body)
            else:
                assert stripped == body
        apply(name, (body + '\n' + body).encode(), False)
        anchor = {'sub_0013AB27': 'ecx = MEM32(edi + 0x30);',
                  'sub_0024FA01': 'MEM32(ecx + 0x40) = eax;',
                  'sub_0024F917': 'loc_0024F950: ;',
                  'sub_0024F964': 'loc_0024F9AC: ;'}[name]
        apply(name, body.replace(anchor, anchor + ' changed').encode(), False)
    print('PASS: all four actual generated owner seams, LF/CRLF, unchanged guest instructions, strict malformed/duplicate anchors')


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    with tempfile.TemporaryDirectory(prefix='mm3-route-diagnostics-') as temporary:
        folder = Path(temporary)
        seams(folder)
        source, executable = folder / 'contract.c', folder / 'contract.exe'
        source.write_text(HARNESS)
        for optimization in ('/Od', '/O2'):
            subprocess.run(['cl', '/nologo', '/W4', optimization, '/std:c11',
                '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'src'),
                '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                '/Fo:' + str(folder / 'contract.obj'), '/Fe:' + str(executable),
                str(source)], cwd=folder, check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
