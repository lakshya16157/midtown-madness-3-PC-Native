"""Exercise native tree-bank fallback, successful-play detection and cooldown.

Run after scripts/enter-env.ps1. Native bank lookup/playback boundaries are
controlled; the production sound hook and complete guest-state/scratch restore
run unchanged. No game data or audio settings are modified.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mm3_recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
static uint64_t clock_ms=1000;
static uint64_t audio_clock(void){return clock_ms;}
#define MM3_TREE_AUDIO_CLOCK audio_clock
#define MM3_TREE_AUDIO_LOG(...) ((void)0)
#include "mm3_tree_audio.c"
static unsigned lookups,plays,constructs,destructs;
static int lookup_ok=1,play_ok=1;
static float last_position[3];
static void dirty(unsigned cleanup){
 g_esp+=cleanup;g_eax=0xEEEE;g_ecx=0xCCCC;g_edx=0xDDDD;
 g_ebx=0xBBBB;g_esi=0x5555;g_edi=0x6666;g_ebp=0x7777;g_seh_ebp=0x8888;g_fs_base=0x9999;g_eflags=0xFFF;
 g_fp_top=7;g_fp_cmp=1;g_df=1;g_fp_cc=0x4100;g_fp_control_word=0x27F;
 for(unsigned i=0;i<8;i++)g_fp_stack[i]=-20.5-i;
 memset(&g_xmm0,0xEF,sizeof g_xmm0);memset(&g_xmm7,0xEF,sizeof g_xmm7);
}
void sub_000125EA(void){
 constructs++;assert(!strcmp((const char *)XBOX_PTR(MEM32(g_esp+4)),"tree"));
 MEM32(g_ecx)=MEM32(g_esp+4);MEM32(g_ecx+4)=4;MEM32(g_esp-24)=0xABABABAB;
 dirty(8);
}
void sub_00198FFB(void){
 lookups++;uint32_t string=MEM32(g_esp+4);
 assert(!strcmp((const char *)XBOX_PTR(MEM32(string)),"tree"));
 MEM32(g_esp-400)=0xCACACACA;dirty(8);g_eax=lookup_ok?0x160000:0;
}
void sub_000127A9(void){destructs++;assert(MEM32(g_esp+4)==1);MEM32(g_ecx)=0;dirty(8);}
void sub_00198592_original(void){
 plays++;uint32_t matrix=MEM32(g_esp+4);
 memcpy(last_position,(const void *)XBOX_PTR(matrix+36),sizeof last_position);
 MEM32(g_esp-800)=0xF0F0F0F0;dirty(8);g_eax=play_ok?0xCAFE0001:0xCAFE0000;
}
void sub_001986B1_original(void){plays++;dirty(16);g_eax=play_ok?1:0;}
static unsigned char *ram,*expected;
static const float position[]={12.5f,-3.25f,99.75f};
static void guest_setup(void){
 g_eax=0x1111;g_ecx=0x2222;g_edx=0x3333;g_esp=0x200000;
 g_ebx=0x4444;g_esi=0x5555;g_edi=0x6666;g_ebp=0x7777;g_seh_ebp=0x8888;g_fs_base=0x9999;g_eflags=0x202;
 g_fp_top=3;g_fp_cmp=-1;g_df=1;g_fp_cc=0x4000;g_fp_control_word=0x37F;
 for(unsigned i=0;i<8;i++)g_fp_stack[i]=42.5+i;
 memset(&g_xmm0,0xA0,sizeof g_xmm0);memset(&g_xmm7,0xA7,sizeof g_xmm7);
}
static void finish(int converted,int contacting,const float *point,float speed){
 guest_setup();AudioGuestState before=audio_save();memcpy(expected,ram,0x300000);
 mm3_tree_audio_finish(converted,contacting,point,speed);AudioGuestState after=audio_save();
 assert(!memcmp(&before,&after,sizeof before));assert(!memcmp(ram,expected,0x300000));
}
static void select(unsigned id){mm3_tree_audio_begin();mm3_tree_audio_select(0x120000,0x130000,id,0x140000);}
static void native_success(unsigned method,uint32_t script,int success){
 guest_setup();mm3_tree_audio_begin();mm3_tree_audio_select(0x120000,0x130000,method,0x140000);
 play_ok=success;g_eax=script;MEM32(g_esp+4)=script;MEM32(g_esp+8)=0x180000;
 const float identity[]={1,0,0,0,1,0,0,0,1,0,0,0};
 memcpy((void *)XBOX_PTR(0x180000),identity,sizeof identity);
 if(method&1){MEM32(g_esp+4)=0x180000;sub_00198592();}else sub_001986B1();
 play_ok=1;
}
int main(void){
 ram=VirtualAlloc(0,0x300000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ram);
 expected=malloc(0x300000);assert(expected);memset(ram,0xA5,0x300000);g_xbox_mem_offset=(ptrdiff_t)ram;
 select(10);finish(1,1,position,20);assert(plays==1&&lookups==1&&constructs==1&&destructs==1);
 assert(!memcmp(last_position,position,sizeof position));
 select(10);finish(1,1,position,20);assert(plays==1&&lookups==1); /* Same physics substep. */
 select(11);finish(1,1,position,20);assert(plays==2); /* Another tree has its own sound. */
 native_success(21,0x160000,1);unsigned before=plays;finish(1,1,position,20);assert(plays==before);
 native_success(22,0x160000,1);before=plays;finish(1,1,position,20);assert(plays==before);
 native_success(23,0x160000,0);before=plays;finish(1,1,position,20);assert(plays==before+1);
 native_success(24,0x170000,1);before=plays;finish(1,1,position,20);assert(plays==before+1);
 select(30);before=plays;finish(0,1,position,3);assert(plays==before+1);
 select(30);finish(0,1,position,3);assert(plays==before+1); /* Solid contact cooldown. */
 select(30);finish(1,1,position,20);assert(plays==before+2); /* Actual break supersedes a gentle tap. */
 select(30);finish(1,1,position,20);assert(plays==before+2);
 clock_ms+=400;select(30);finish(0,1,position,3);assert(plays==before+3);
 before=plays;
 select(40);finish(0,0,position,20);select(41);finish(0,1,position,1.99f);
 select(42);finish(1,1,position,NAN);select(43);finish(1,1,NULL,20);
 const float invalid[]={NAN,0,0};select(44);finish(1,1,invalid,20);assert(plays==before);
 mm3_tree_audio_begin();finish(1,1,position,20);assert(plays==before); /* No tree selected. */
 lookup_ok=0;select(50);finish(1,1,position,20);assert(plays==before);
 select(50);unsigned lookup_before=lookups;finish(1,1,position,20);assert(lookups==lookup_before);
 lookup_ok=1;clock_ms+=400;select(50);finish(1,1,position,20);assert(plays==before+1);
 before=plays;mm3_tree_audio_begin();mm3_tree_audio_select(0x120000,0x131000,50,0x140000);
 finish(1,1,position,20);assert(plays==before+1); /* Reloaded city permits its own instance. */
 assert(constructs==destructs&&lookups==constructs);
 puts("PASS: tree bank native success/failure, both playback methods, unrelated sounds, new breaks and solid contact cooldowns, separate instances/cities, invalid data and full guest-state/scratch restoration");
 free(expected);VirtualFree(ram,0,MEM_RELEASE);return 0;
}
'''


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    with tempfile.TemporaryDirectory(prefix='mm3-tree-audio-') as temporary:
        directory = Path(temporary)
        code, executable = directory / 'audio.c', directory / 'audio.exe'
        code.write_text(SOURCE)
        subprocess.run(['cl', '/nologo', '/Od', '/MD', '/std:c11',
                        '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                        '/Fo' + str(directory / 'audio.obj'), '/Fe' + str(executable), str(code)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
