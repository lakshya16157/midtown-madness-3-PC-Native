"""Exercise detached-tree rendering through the actual native renderer.

Run with tools/venv/Scripts/python.exe after scripts/enter-env.ps1. The
production draw wrapper and CMake mesh seam run with generated 16AEDB and
both native matrix-copy queues. Only graphics submission, visibility, effects
and arithmetic leaves are modeled; no game, graphics device or saves run.
"""
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import mm3_tree_contact_regression as contact

ROOT = contact.ROOT
ENTRIES = (0x16AEDB, 0x169AD3, 0x169A15)

C_TEST = r'''
#define MM3_TREE_TEST_HELPERS
#define MM3_TREE_LOG(...) ((void)0)
#define MM3_TREE_FLUSH() ((void)0)
#include "mm3_tree_diagnostics.c"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL native render line %d: %s\n",__LINE__,#c);exit(2);}}while(0)
#define RAM_SIZE 0x1000000u
#define STACK 0x700000u
#define LOCAL 0x7C0000u
#define OWNER 0x873000u
#define MODEL 0x874000u
#define MATRIX 0x880000u
#define SCENE 0x8A0000u
#define CAMERA 0x8C0000u
#define VIEW 0x8D0000u
#define POOL 0x8E0000u
static unsigned char *ram,*initial,*direct_ram;
static unsigned cases;
static const float transforms[][12]={
 {0,0,-2, 1.2f,.9f,0, .45f,-.6f,0, 120,4,-75},
 {0,0,-2, 1.5f,0,0, 0,-.75f,0, -14,2,90},
 {0,0,-2, 0,1.5f,0, .75f,0,0, 120,4,-75}
};
typedef struct {
 uint32_t regs[11];int top,direction,compare;uint16_t status,control;
 double fp[8];RecompXmm xmm[8];
} GuestState;
static GuestState save_state(void){
 GuestState state;memset(&state,0,sizeof state);
 uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};
 RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
 memcpy(state.regs,regs,sizeof regs);memcpy(state.xmm,xmm,sizeof xmm);
 memcpy(state.fp,g_fp_stack,sizeof state.fp);state.top=g_fp_top;
 state.direction=g_df;state.compare=g_fp_cmp;state.status=g_fp_cc;state.control=g_fp_control_word;
 return state;
}
static void restore_state(const GuestState *state){
 g_eax=state->regs[0];g_ecx=state->regs[1];g_edx=state->regs[2];g_esp=state->regs[3];
 g_ebx=state->regs[4];g_esi=state->regs[5];g_edi=state->regs[6];g_ebp=state->regs[7];
 g_seh_ebp=state->regs[8];g_fs_base=state->regs[9];g_eflags=state->regs[10];
 g_fp_top=state->top;g_df=state->direction;g_fp_cmp=state->compare;
 g_fp_cc=state->status;g_fp_control_word=state->control;
 memcpy(g_fp_stack,state->fp,sizeof state->fp);
 g_xmm0=state->xmm[0];g_xmm1=state->xmm[1];g_xmm2=state->xmm[2];g_xmm3=state->xmm[3];
 g_xmm4=state->xmm[4];g_xmm5=state->xmm[5];g_xmm6=state->xmm[6];g_xmm7=state->xmm[7];
}
static void registers(void){
 g_eax=0x11223344;g_ecx=0x22334455;g_edx=0x33445566;
 g_ebx=0x44556677;g_esi=0x55667788;g_edi=0x66778899;
 g_ebp=g_seh_ebp=0x778899AA;g_fs_base=0x8899AABB;g_eflags=0x202;
 g_fp_top=3;g_df=0;g_fp_cmp=-1;g_fp_cc=0x4100;g_fp_control_word=0x27F;
 for(unsigned i=0;i<8;i++)g_fp_stack[i]=i*1.25-3;
 memset(&g_xmm0,0xA0,sizeof g_xmm0);memset(&g_xmm1,0xA1,sizeof g_xmm1);
 memset(&g_xmm2,0xA2,sizeof g_xmm2);memset(&g_xmm3,0xA3,sizeof g_xmm3);
 memset(&g_xmm4,0xA4,sizeof g_xmm4);memset(&g_xmm5,0xA5,sizeof g_xmm5);
 memset(&g_xmm6,0xA6,sizeof g_xmm6);memset(&g_xmm7,0xA7,sizeof g_xmm7);
}
static void arguments(uint32_t stack,uint32_t caller,uint32_t matrix){
 g_esp=stack;memset((void *)XBOX_PTR(stack),0,40);
 MEM32(stack)=caller;MEM32(stack+4)=MODEL;MEM32(stack+8)=CAMERA;
 MEM32(stack+12)=VIEW;MEM32(stack+24)=matrix;
}
static void setup(uint32_t caller,unsigned flags,unsigned accepted,const float matrix[12]){
 memcpy(ram,initial,RAM_SIZE);registers();arguments(STACK,caller,MATRIX);
 MEM32(MODEL)=OWNER;MEM32(MODEL+0xA0)=LOCAL;MEM32(MODEL+0xC8)=flags;
 MEM8(OWNER+0xA0)=1;MEM8(OWNER+0xA2)=0;MEM32(OWNER+0xA4)=SCENE;
 MEM32(SCENE+0x1521C)=0;memcpy((void *)XBOX_PTR(MATRIX),matrix,48);
 billboard_calls=mesh_calls=0;billboard_result=accepted;mesh_observed_scope=0;
 native_queue=0;nest_requested=nest_completed=0;
 CHECK(!mm3_tree_fall_drawing());
}
static void execute(unsigned wrapped,const float matrix[12]){
 if(wrapped)sub_0016AEDB();else sub_0016AEDB_original();
 CHECK(g_esp==STACK+40&&!mm3_tree_fall_drawing());
 CHECK(!memcmp((const void *)XBOX_PTR(MATRIX),matrix,48));
}
static void test_nested_render(void){
 if(!nest_requested)return;
 nest_requested=0;CHECK(mm3_tree_fall_drawing());
 GuestState state=save_state();
 memcpy((void *)XBOX_PTR(MATRIX+0x100),transforms[2],48);
 arguments(STACK+0x1000,0x0015E4D9u,MATRIX+0x100);
 sub_0016AEDB();
 CHECK(g_esp==STACK+0x1028&&mm3_tree_fall_drawing());
 CHECK(!memcmp((const void *)XBOX_PTR(MATRIX+0x100),transforms[2],48));
 restore_state(&state);nest_completed=1;
}
void sub_0016CABD(void){CHECK(0);}
void sub_000E1F49_original(void){CHECK(0);}
void sub_001D137B_original(void){CHECK(0);}
int main(int argc,char **argv){
 CHECK(argc==5);ram=VirtualAlloc(0,RAM_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 initial=malloc(RAM_SIZE);direct_ram=malloc(RAM_SIZE);CHECK(ram&&initial&&direct_ram);
 FILE *input=fopen(argv[1],"rb");CHECK(input&&fread(initial,1,RAM_SIZE,input)==RAM_SIZE);fclose(input);
 g_xbox_mem_offset=(ptrdiff_t)ram;_putenv_s("MM3_TREE_TRACE",argv[2]);
 unsigned patched=atoi(argv[3]),tree=atoi(argv[4]),queues=0;
 const uint32_t callers[]={0x00167D94u,0x0015C05Du,0x0015E4D9u,0xDEADBEEFu};
 const unsigned flags[]={0,0x0442};
 for(unsigned transform=0;transform<3;transform++)for(unsigned flag=0;flag<2;flag++)
 for(unsigned accepted=0;accepted<2;accepted++)for(unsigned caller=0;caller<4;caller++){
  const float *matrix=transforms[transform];
  setup(callers[caller],flags[flag],accepted,matrix);execute(0,matrix);
  GuestState direct=save_state();memcpy(direct_ram,ram,RAM_SIZE);
  unsigned direct_billboard=billboard_calls,direct_mesh=mesh_calls;
  setup(callers[caller],flags[flag],accepted,matrix);execute(1,matrix);
  unsigned force=tree&&caller==0&&patched;
  CHECK(billboard_calls==(force?0u:1u));CHECK(mesh_calls==(force||!accepted?1u:0u));
  if(mesh_calls){
   CHECK(!memcmp((const void *)XBOX_PTR(POOL+8),matrix,48));
   CHECK(mesh_observed_scope==(tree&&caller==0?1u:0u));queues|=1u<<native_queue;
  }
  if(!force){
   GuestState wrapped=save_state();CHECK(!memcmp(&direct,&wrapped,sizeof direct));
   CHECK(billboard_calls==direct_billboard&&mesh_calls==direct_mesh);
   CHECK(!memcmp(direct_ram,ram,RAM_SIZE));
  }
  ++cases;
 }
 CHECK(queues==6u); /* Both native matrix-copy queue branches executed. */
 if(patched&&tree){
  setup(0x00167D94u,0,0,transforms[0]);nest_requested=1;execute(1,transforms[0]);
  CHECK(nest_completed&&billboard_calls==1&&mesh_calls==2);
  CHECK(!memcmp((const void *)XBOX_PTR(POOL+8),transforms[0],48));++cases;
 }
 printf("PASS %u actual native renderer cases: tree=%u patched=%u trace=%s; tilted/flat transforms, both mesh queues, unchanged static and unrelated draws%s\n",
  cases,tree,patched,argv[2],patched&&tree?", nested draw scope":"");
 free(direct_ram);free(initial);VirtualFree(ram,0,MEM_RELEASE);return 0;
}
'''


def apply_mesh_seam(directory, source):
    original, output = directory / 'before.c', directory / 'after.c'
    original.write_text(source)
    script = directory / 'mesh.cmake'
    script.write_text(f'''include("{(ROOT / 'scripts/mm3_tree_contact_seam.cmake').as_posix()}")
file(READ "{original.as_posix()}" content)
mm3_add_tree_mesh(content)
file(WRITE "{output.as_posix()}" "${{content}}")
''')
    subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True, text=True)
    return output.read_text()


def native_source(directory, patched):
    previous = contact.common.ENTRIES
    try:
        contact.common.ENTRIES = ENTRIES
        source = contact.common.native_source().split('int main(int argc,char**argv){', 1)[0]
    finally:
        contact.common.ENTRIES = previous
    if patched:
        source = apply_mesh_seam(directory, source)
    source = source.replace('void sub_0016AEDB(void)\n{',
                            'void sub_0016AEDB_original(void)\n{', 1)
    # Execute the native LOD choice and native copies. These leaves represent
    # only external graphics allocation/submission and fixed visibility input.
    stubs = {
        'sub_001C7D01': 'g_esp+=12;',
        'sub_000139C8': 'for(unsigned i=0;i<3;i++)MEM32(g_eax+i*4)=MEM32(g_edx+i*4);g_esp+=4;',
        'sub_001C7B15': 'g_eax=0;g_esp+=8;',
        'sub_0016C29E': 'g_fp_top=(g_fp_top+7)&7;g_fp_stack[g_fp_top]=1.0;g_esp+=4;',
        'sub_0023BB76': 'g_eax=0;g_esp+=20;',
        'sub_00164801': 'g_eax=0;g_esp+=4;',
        'sub_0016A909': 'mesh_calls++;mesh_observed_scope=mm3_tree_fall_drawing();native_queue=2;test_nested_render();g_eax=0x8E0000u;g_esp+=4;',
        'sub_00168BDD': 'mesh_calls++;mesh_observed_scope=mm3_tree_fall_drawing();native_queue=1;test_nested_render();g_eax=0x8E0000u;g_esp+=4;',
        'sub_0016946B': 'billboard_calls++;g_eax=billboard_result;g_esp+=8;',
        'sub_000129CD': 'g_eax=0;g_esp+=8;',
        'sub_00019D3A': 'g_eax=0x8F0000u;g_esp+=4;',
    }
    for name, replacement in stubs.items():
        source, count = re.subn(r'void ' + name + r'\(void\)\{.*?\}',
                               lambda _: f'void {name}(void){{{replacement}}}', source, count=1)
        assert count == 1, name
    return ('#define WIN32_LEAN_AND_MEAN\n#include <windows.h>\n'
            'static unsigned billboard_calls,mesh_calls,billboard_result,mesh_observed_scope;\n'
            'static unsigned native_queue,nest_requested,nest_completed;\n'
            'int mm3_tree_fall_drawing(void);\nstatic void test_nested_render(void);\n'
            + source + C_TEST)


def main():
    if not shutil.which('cl') or not shutil.which('cmake'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    image = contact.common.common.Image(str(ROOT / 'game_files/default.xbe'))
    # Pin the genuine native dynamic render caller, rather than accepting an
    # arbitrary magic address supplied only by the test.
    renderer_calls = (ROOT / 'src/recomp/gen/recomp_0014.c').read_text()
    assert 'PUSH32(esp, 0x00167D94u); RECOMP_ABI_CALL(0x0016AEDBu, sub_0016AEDB)' in renderer_calls
    with tempfile.TemporaryDirectory(prefix='mm3-tree-native-render-') as temporary:
        directory = Path(temporary)
        executables = {}
        for patched in (False, True):
            name = 'mesh' if patched else 'billboard-control'
            code, executable = directory / (name + '.c'), directory / (name + '.exe')
            code.write_text(native_source(directory, patched))
            subprocess.run(['cl', '/nologo', '/Od', '/MD', '/std:c11',
                            '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(ROOT / 'src'),
                            '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                            '/Fo' + str(directory / (name + '.obj')), '/Fe' + str(executable),
                            str(code)], check=True, cwd=directory)
            executables[patched] = executable
        runs = 0
        for tree, kind in ((True, 'tree-event'), (False, 'other-event')):
            fixture = directory / ('tree.bin' if tree else 'trashcan.bin')
            fixture.write_bytes(contact.fixture(image, 2, 30, kind=kind))
            for tracing in ('0', '1', 'contacts'):
                for patched in (False, True):
                    subprocess.run([str(executables[patched]), str(fixture), tracing,
                                    str(int(patched)), str(int(tree))], check=True)
                    runs += 1
        print(f'PASS: {runs} native renderer/production wrapper runs, including the '
              'unmodified billboard negative control; no game or graphics device required.')


if __name__ == '__main__':
    main()
