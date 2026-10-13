"""Execute native detached-prop construction, pose, velocities and registration.

The production helper and real 216ACA constructor execute against retail XBE
constants and native string methods. Geometry I/O, allocation and the physics
backend are isolated leaves; assertions observe the actual collider arrays and
native manager handoff. No game process, graphics device or save is required.
"""
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

import mm3_tree_contact_regression as contact

ROOT = contact.ROOT
ENTRIES = (0x216ACA, 0x216398, 0x1AFB4, 0x1F196C, 0x2163BD,
           0x125EA, 0x127A9, 0x1270E, 0x12610, 0x1269F, 0x12731, 0x1274D, 0x1291C, 0x12000, 0x19115, 0x1944A)
SUPPORT = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(2);}}while(0)
#define CITY 0x800000u
#define MANAGER 0x810000u
#define OBJECT 0x820000u
#define GEO 0x830000u
#define ROOT_BODY 0x840000u
#define RIGID_BODY 0x850000u
#define NODE 0x860000u
#define CONTROLLER 0x870000u
#define ALLOC 0x900000u
#define STACK 0x700000u
#define RAM_SIZE 0x1000000u
static unsigned allocation_calls,physics_calls,registered,global_registered,destroyed,freed,pose_calls,points_seen,planes_seen,evicted;
static unsigned fail_allocation,fail_physics,bad_geometry,missing_manager;
static uint32_t allocated=ALLOC;
static float received_pose[12];
static const float world[12]={0,0,-1,0,1.5f,0,1,0,0,120,4,-75};
static const float points[8][3]={{-1,0,-1},{1,0,-1},{1,10,-1},{-1,10,-1},{-1,0,1},{1,0,1},{1,10,1},{-1,10,1}};
static const float planes[6][4]={{-1,0,0,1},{1,0,0,1},{0,-1,0,0},{0,1,0,10},{0,0,-1,1},{0,0,1,1}};
static void initialize_geometry(uint32_t p){
 memset((void *)XBOX_PTR(p),0,0x27C);MEM32(p)=0x5F0000;
 memcpy((void *)XBOX_PTR(p+8),world,sizeof world);MEM32(p+0x44)=bad_geometry?4:0;
 MEM32(p+0x160)=8;MEM32(p+0x264)=6;
 memcpy((void *)XBOX_PTR(p+0xA0),points,sizeof points);
 memcpy((void *)XBOX_PTR(p+0x164),planes,sizeof planes);
}
static void destroy_leaf(void){destroyed++;global_registered=0;MEM32(OBJECT+8)=0;g_esp+=8;}
static void geometry_destroy_leaf(void){freed++;g_esp+=8;}
static void point_count(void){g_eax=MEM32(g_ecx+0x160);g_esp+=4;}
static void plane_count(void){g_eax=MEM32(g_ecx+0x264);g_esp+=4;}
static void point_at(void){uint32_t out=MEM32(g_esp+4),n=MEM32(g_esp+8);memcpy((void *)XBOX_PTR(out),(void *)XBOX_PTR(g_ecx+0xA0+n*12),12);g_eax=out;g_esp+=12;}
static void plane_at(void){uint32_t out=MEM32(g_esp+4),n=MEM32(g_esp+8);memcpy((void *)XBOX_PTR(out),(void *)XBOX_PTR(g_ecx+0x164+n*16),16);g_eax=out;g_esp+=12;}
static void physics_init(void){
 points_seen=8;planes_seen=6;
 CHECK(MEM32(g_esp+12)&&MEM32(g_esp+16));
 CHECK(!memcmp((void *)XBOX_PTR(MEM32(g_esp+12)),points,sizeof points));
 CHECK(!memcmp((void *)XBOX_PTR(MEM32(g_esp+16)),planes,sizeof planes));
 g_esp+=20;
}
static void physics_pose(void){uint32_t matrix=MEM32(g_esp+4);memcpy(received_pose,(void *)XBOX_PTR(matrix),sizeof received_pose);pose_calls++;g_esp+=8;}
static void physics_enable(void){CHECK(MEM32(g_esp+4)==1);g_esp+=8;}
static void noop_physics(void){g_esp+=4;}
static void physics_aux(void){g_esp+=8;}
'''

STUBS = {
    'sub_001808CB': 'g_eax=missing_manager?0:MANAGER;g_esp+=8;',
    'sub_0016FF04': '''unsigned size=MEM32(g_esp+4);allocation_calls++;
      if(allocation_calls==fail_allocation)g_eax=0;
      else if(size==0xB0)g_eax=OBJECT;else if(size==0x27C)g_eax=GEO;
      else {g_eax=allocated;allocated+=(size+15)&~15u;}g_esp+=8;''',
    'sub_0016E41F': '''CHECK(g_ecx==CITY&&MEM32(g_esp+4)==0x800043);
      PUSH32(g_esp,0x27C);PUSH32(g_esp,0x0015CD97);
      CHECK(mm3_tree_body_geometry_allocation());CHECK(g_eax==GEO);
      initialize_geometry(g_eax);g_esp+=8;''',
    'sub_00170EC0': 'freed++;g_esp+=4;',
    'sub_0001259E': 'g_esp+=20;',
    'sub_0001260D': 'g_eax=g_ecx;g_esp+=4;',
    'sub_00012BFA': 'memcpy((void *)XBOX_PTR(g_ecx),world,sizeof world);g_esp+=4;',
    'sub_001C032D': 'g_eax=0;g_esp+=4;',
    'sub_001C0107': 'g_eax=CONTROLLER;g_esp+=4;',
    'sub_00170EE2': 'uint32_t size=g_eax;g_eax=allocated;allocated+=(size+15)&~15u;g_esp+=4;',
    'sub_000DDDDD': 'g_eax=0;g_esp+=8;',
    'sub_001D8E24': 'g_esp+=32;',
    'sub_002474E5': '''physics_calls++;CHECK(MEM32(g_esp+4)==8&&g_edi==6);
      MEM32(ROOT_BODY)=0x5F0100;MEM32(ROOT_BODY+0x14)=RIGID_BODY-0x100;MEM32(RIGID_BODY-0x100)=RIGID_BODY;MEM32(RIGID_BODY+0x8C)=0x3F800000;MEM32(RIGID_BODY+0x90)=0x3F800000;MEM32(RIGID_BODY+0x94)=0x3F800000;
      g_eax=fail_physics?0:ROOT_BODY;g_esp+=8;''',
    'sub_0012AF23': 'CHECK(MEM32(g_esp+4)==GEO);memcpy((void *)XBOX_PTR(g_ebx),world,sizeof world);g_eax=g_ebx;g_esp+=8;',
    'sub_00217788': 'global_registered++;CHECK(MEM32(g_esp+4)==OBJECT);g_esp+=8;',
    'sub_0012ABEC': 'g_esp+=4;',
    'sub_0003F85B': '''CHECK(MEM32(g_esp+4)==MANAGER+0x10);
      CHECK(MEM32(MEM32(g_esp+12))==OBJECT);registered++;MEM32(MANAGER+0x18)++;g_esp+=16;''',
    'sub_001F054D': 'CHECK(g_edi==MANAGER);g_eax=OBJECT+0x100;g_esp+=4;',
    'sub_00216313': 'g_eax=1;g_esp+=4;',
    'sub_001F1777': 'g_esp+=8;',
    'sub_001F1512': '''CHECK(g_esi==MANAGER);if(MEM32(g_esp+4)==OBJECT+0x100){evicted++;MEM32(MANAGER+0x18)--;}
      else{CHECK(MEM32(g_esp+4)==OBJECT);registered--;}g_esp+=8;''',
    'sub_0002B634': 'g_esp+=8;',
    'sub_00012A76': 'g_eax=(uint32_t)strlen((char *)XBOX_PTR(MEM32(g_esp+4)));g_esp+=8;',
}

TEST = r'''
#include "mm3_tree_body.c"
typedef struct {uint32_t r[11];int f[3];uint16_t c[2];double fp[8];RecompXmm xmm[8];} State;
static State state(void){State s;memset(&s,0,sizeof s);uint32_t r[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags};memcpy(s.r,r,sizeof r);s.f[0]=g_fp_top;s.f[1]=g_df;s.f[2]=g_fp_cmp;s.c[0]=g_fp_cc;s.c[1]=g_fp_control_word;memcpy(s.fp,g_fp_stack,sizeof s.fp);RecompXmm x[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};memcpy(s.xmm,x,sizeof x);return s;}
static void setup(unsigned char *ram,const unsigned char *initial){
 memcpy(ram,initial,RAM_SIZE);allocation_calls=physics_calls=registered=global_registered=destroyed=freed=pose_calls=points_seen=planes_seen=evicted=0;allocated=ALLOC;
 MEM32(0x4291F0)=CITY;MEM32(MANAGER+0x20)=CITY;MEM32(MANAGER+0x1C)=CONTROLLER;
 MEM32(0x5F0000)=0x5F0200;MEM32(0x5F0004)=0x5F0204;MEM32(0x5F0008)=0x5F0208;MEM32(0x5F000C)=0x5F020C;MEM32(0x5F0010)=0x5F0210;
 MEM32(0x5F0100)=0x5F0300;MEM32(0x5F0118)=0x5F0304;MEM32(0x5F0128)=0x5F0308;
 MEM32(0x384B50)=0x5F0400;
 g_eax=0x1111;g_ecx=0x2222;g_edx=0x3333;g_esp=STACK;g_ebx=0x4444;g_esi=0x5555;g_edi=0x6666;g_ebp=0x7777;g_seh_ebp=0x8888;g_fs_base=0x9999;g_eflags=0x202;
 g_fp_top=3;g_df=1;g_fp_cmp=-1;g_fp_cc=0x4100;g_fp_control_word=0x27F;for(unsigned j=0;j<8;j++)g_fp_stack[j]=77.5+j;memset(&g_xmm0,0xA0,sizeof g_xmm0);memset(&g_xmm7,0xA7,sizeof g_xmm7);
}
int main(int argc,char **argv){
 CHECK(argc==2);unsigned char *ram=VirtualAlloc(0,RAM_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE),*initial=malloc(RAM_SIZE),saved_stack[16384];CHECK(ram&&initial);
 FILE *in=fopen(argv[1],"rb");CHECK(in&&fread(initial,1,RAM_SIZE,in)==RAM_SIZE);fclose(in);g_xbox_mem_offset=(ptrdiff_t)ram;
 const float velocity[3]={18,4,24};float position[3]={0};
 for(unsigned guard=0;guard<9;guard++){
  fail_allocation=guard==1?1:guard==2?2:0;fail_physics=guard==3;bad_geometry=guard==4;missing_manager=guard==5;
  setup(ram,initial);if(guard==6)MEM32(MANAGER+0x20)=CITY+4;
  if(guard==7)MEM32(MANAGER+0x18)=16;if(guard==8)MEM32(MANAGER+0x18)=17;
  State before=state();memcpy(saved_stack,ram+STACK-16384,sizeof saved_stack);
  int result=mm3_tree_body_create(CITY,0x800043,0x7C0000,velocity,position);State after=state();
  CHECK(!memcmp(&before,&after,sizeof before));CHECK(!memcmp(saved_stack,ram+STACK-16384,sizeof saved_stack));
  CHECK(result==(guard==0||guard==7));
  if(guard==0||guard==7){
   CHECK(physics_calls==1&&registered==1&&global_registered==1&&points_seen==8&&planes_seen==6);
   CHECK(pose_calls==2&&!memcmp(received_pose,world,sizeof world));
   CHECK(!memcmp(position,world+9,sizeof position));CHECK(MEM32(GEO+0x9C)==0x800043);
   CHECK(fabsf(MEMF(RIGID_BODY+0x20)-6.3f)<.0001f&&fabsf(MEMF(RIGID_BODY+0x24)-1.4f)<.0001f&&fabsf(MEMF(RIGID_BODY+0x28)-8.4f)<.0001f);
   CHECK(fabsf(MEMF(RIGID_BODY+0x5C)-1.6f)<.0001f&&MEMF(RIGID_BODY+0x60)==0&&fabsf(MEMF(RIGID_BODY+0x64)+1.2f)<.0001f);
   CHECK(MEM8(OBJECT+0x15)==1);CHECK(MEM32(OBJECT+0x5C+0x14)==6); /* native copied string */
   CHECK(!strcmp((const char *)XBOX_PTR(OBJECT+0x5C+4),"object"));
   mm3_tree_body_confirm(1);CHECK(registered==1&&destroyed==0);
   CHECK(evicted==(guard==7));if(guard==7)CHECK(MEM32(MANAGER+0x18)==16);
  }else CHECK(registered==0);
 }
 setup(ram,initial);fail_allocation=fail_physics=bad_geometry=missing_manager=0;
 CHECK(mm3_tree_body_create(CITY,0x800043,0x7C0000,velocity,position));mm3_tree_body_confirm(0);CHECK(registered==0&&destroyed==1&&global_registered==0);
 puts("PASS: real native216ACA construction, authored point/plane handoff,125EA/12610 string ABI, rigid-body pose/velocities, physical manager registration and native capacity eviction, allocation/type/controller/failure guards, rollback and full guest/stack preservation");
 return 0;
}
'''


def source():
    previous = contact.common.ENTRIES
    try:
        contact.common.ENTRIES = ENTRIES
        code = contact.common.native_source().split('int main(int argc,char**argv){', 1)[0]
    finally:
        contact.common.ENTRIES = previous
    code = code.replace('default:fprintf(stderr,"unexpected dispatch %X\\n",va);', '''
case 0x5F0200:return geometry_destroy_leaf;case 0x5F0204:return point_count;case 0x5F0208:return point_at;
case 0x5F020C:return plane_count;case 0x5F0210:return plane_at;
case 0x5F0300:return physics_init;case 0x5F0304:return physics_pose;case 0x5F0308:return physics_enable;
case 0x5F0400:return destroy_leaf;
default:fprintf(stderr,"unexpected dispatch %X\\n",va);''')
    # Definitions needed exclusively by the production helper do not appear
    # in generated references; make those leaves explicit in this fixture.
    for name in STUBS:
        if f'void {name}(void)' not in code:
            code += f'\nvoid {name}(void){{fprintf(stderr,"unexpected {name}\\n");exit(99);}}\n'
    for name, body in STUBS.items():
        code, n = re.subn(r'void ' + name + r'\(void\)\{.*?\}',
                         lambda match: f'void {name}(void){{{body}}}', code, count=1)
        assert n == 1, name
    code = code.replace('#include <stdio.h>', '#include <stdio.h>\nint mm3_tree_body_geometry_allocation(void);')
    code = code.replace('g_xbox_code_hi=0x260000', 'g_xbox_code_hi=0x600000')
    return code[:code.index('void sub_')] + SUPPORT + code[code.index('void sub_'):] + TEST


def check_geometry_seams(directory):
    source = (ROOT / 'src/recomp/gen/recomp_0014.c').read_text()
    cases = 0
    for owner in ('sub_0015CD35', 'sub_0015B9AB'):
        body = re.search(r'void ' + owner + r'\(void\)\n\{.*?\n\}', source, re.S).group()
        for newline in ('\n', '\r\n'):
            original = directory / 'geometry-before.c'
            output = directory / 'geometry-after.c'
            script = directory / 'geometry.cmake'
            script.write_text(f'''include("{(ROOT / 'scripts/mm3_tree_contact_seam.cmake').as_posix()}")
file(READ "{original.as_posix()}" content)
mm3_add_tree_geometry(content "{owner}")
file(WRITE "{output.as_posix()}" "${{content}}")
''')
            original.write_bytes(body.replace('\n', newline).encode())
            subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True)
            patched = output.read_text()
            assert patched.count('if (!mm3_tree_body_geometry_allocation())') == 1
            recovered = patched[patched.index('void ' + owner):].replace(
                'if (!mm3_tree_body_geometry_allocation()) ', '')
            assert recovered == body
            cases += 1
            for invalid in (body + body, patched, body.replace('0x0016FF04u', '0x0016FF05u')):
                original.write_bytes(invalid.replace('\n', newline).encode())
                result = subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)
                assert result.returncode and 'MM3 tree geometry:' in result.stderr
                cases += 1
    return cases


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    image = contact.common.common.Image(str(ROOT / 'game_files/default.xbe'))
    with tempfile.TemporaryDirectory(prefix='mm3-tree-body-') as temporary:
        directory = Path(temporary)
        code = directory / 'body.c'
        executable = directory / 'body.exe'
        seams = check_geometry_seams(directory)
        code.write_text(source())
        subprocess.run(['cl', '/nologo', '/Od', '/MD', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                        '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                        '/Fo' + str(directory / 'body.obj'), '/Fe' + str(executable), str(code)], check=True, cwd=directory)
        fixture = directory / 'fixture.bin'
        fixture.write_bytes(contact.fixture(image, 2, 30))
        subprocess.run([str(executable), str(fixture)], check=True)
        print(f'PASS: {seams} exact native geometry allocation seams/fail-closed controls across both instance classes and newline formats.')


if __name__ == '__main__':
    main()
