"""Check real tree contact/audio glue for both cities and solid impacts.

The production metadata classifier, contact wrapper, bank-success hooks and
fallback run together. Native physics and sound-bank calls are controlled to
isolate cooldown drops, invalid contacts and failed bank playback.
"""
import shutil
import subprocess
import tempfile
from pathlib import Path

import mm3_tree_audio_regression as audio

ROOT = Path(__file__).resolve().parents[1]
PREFIX = audio.SOURCE[:audio.SOURCE.index('int main(void){')]
PREFIX = PREFIX.replace('#include "mm3_tree_audio.c"', '''#include "mm3_tree_audio.c"
#define MM3_TREE_CLOCK audio_clock
#define MM3_TREE_LOG(...) ((void)0)
#define MM3_TREE_FLUSH() ((void)0)
#include "mm3_tree_diagnostics.c"
/* Collision profiling is opt-in (RECOMP_COLLISION_PROFILE); keep it off. */
int mm3_collision_profile_on;
MM3CollisionSample mm3_collision_profile_begin(uint32_t car, uint32_t contacts)
{MM3CollisionSample sample={0};(void)car;(void)contacts;return sample;}
void mm3_collision_profile_end(MM3CollisionSample sample, uint32_t contacts){(void)sample;(void)contacts;}
void mm3_collision_profile_effect(void){}''')
INTEGRATION = r'''
static const uint32_t CONTACT=0x110000,METADATA=0x120000,CITY=0x130000;
static const uint32_t CAR=0x140000,SIM=0x141000,BODY=0x150000;
static const uint32_t EVENT=0x170000,NAME=0x175000;
static float contact_speed;
static int actual_solid=1,native_mode;
void sub_0016AEDB_original(void){assert(0);}
void sub_000E1F49_original(void){assert(0);}
void sub_0016CABD(void){assert(0);}
int mm3_tree_body_create(uint32_t city,uint32_t id,uint32_t metadata,const float velocity[3],float point[3]){
 assert(0);return 0;
}
void mm3_tree_body_confirm(int retired){assert(0);}
void sub_001D137B_original(void){
 int tree=mm3_tree_use_solid_contact(CONTACT,contact_speed);
 if(actual_solid)MEM32(0x3C0344)++;
 if(native_mode){
  uint32_t saved_stack=g_esp;
  float matrix[12]={1,0,0,0,1,0,0,0,1,12.5f,-3.25f,99.75f};
  memcpy((void *)XBOX_PTR(0x180000),matrix,sizeof matrix);
  g_eax=native_mode==3?0x160004:0x160000;
  MEM32(g_esp+4)=0x180000;
  play_ok=native_mode!=2;sub_00198592();play_ok=1;g_esp=saved_stack;
 }
 MEMF(BODY+0x20)=0; /* Solver may stop the car: retain pre-impact speed. */
 g_esp+=12;
}
static void interned(const char *name){
 unsigned count=(unsigned)strlen(name)+1;
 memset((void *)XBOX_PTR(NAME),0,256);
 for(unsigned i=0;i<count;i++){
  unsigned ch=i?(unsigned char)name[count-1-i]:0;
  uint32_t address=NAME+16*i,parent=i+1<count?address+16:0;
  MEM32(address)=parent|(ch>>4);MEM32(address+4)=ch&15;
  MEM32(address+8)=i?address-16:0;
 }
}
static void setup(int main_sound,const char *name,unsigned id,float speed){
 memset((void *)XBOX_PTR(METADATA),0,52);
 memset((void *)XBOX_PTR(EVENT),0,86);
 memset((void *)XBOX_PTR(CITY),0,0x15400);
 interned(name);
 if(main_sound)MEM32(METADATA+0x10)=NAME;
 else{MEM8(METADATA)=1;MEM32(METADATA+0x2C)=EVENT;MEM32(EVENT+0x27)=NAME;MEM32(EVENT+0xD)=1;}
 MEM32(0x4291F0)=CITY;MEM32(0x3C0344)=0;
 MEM16(CONTACT+0x100)=2;MEM32(CONTACT+0x104)=id;MEM32(CONTACT+0x108)=METADATA;
 MEM32(CAR+0x3C)=SIM;MEM32(SIM+4)=BODY;
 memcpy((void *)XBOX_PTR(BODY+0xC8),position,sizeof position);
 MEMF(BODY+0x20)=speed;MEMF(BODY+0x24)=MEMF(BODY+0x28)=0;
 contact_speed=speed;guest_setup();MEM32(g_esp)=0x600000;MEM32(g_esp+4)=CAR;MEM32(g_esp+8)=0;
}
static void drive(int main_sound,const char *name,unsigned id,float speed){
 setup(main_sound,name,id,speed);sub_001D137B();
 assert(g_esp==0x20000C);
}
int main(void){
 ram=VirtualAlloc(0,0x500000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ram);
 expected=malloc(0x500000);assert(expected);memset(ram,0,0x500000);g_xbox_mem_offset=(ptrdiff_t)ram;
 /* Both authored representations must reach fallback even when native event
  * playback was dropped by a previous prop's shared collision cooldown. */
 unsigned before=plays;
 drive(1,"tree",10,3);assert(plays==before+1);assert(!memcmp(last_position,position,sizeof position));
 drive(0,"tree",11,3);assert(plays==before+2);assert(!memcmp(last_position,position,sizeof position));
 drive(0,"tree",11,3);assert(plays==before+2); /* Same selected physics contact. */
 clock_ms+=400;drive(0,"tree",11,3);assert(plays==before+3);
 before=plays;drive(1,"tree",12,1.99f);drive(0,"tree",13,1.99f);assert(plays==before);
 drive(1,"trashcan",14,3);drive(0,"trashcan",15,3);assert(plays==before);
 actual_solid=0;drive(1,"tree",16,3);drive(0,"tree",17,3);assert(plays==before);
 actual_solid=1;
 /* Successful native bank playback suppresses fallback in both cities; a
  * failed attempt or an unrelated native sound must not suppress it. */
 for(int city=0;city<2;city++){
  native_mode=1;before=plays;drive(city,"tree",20+city,3);assert(plays==before+1);
  native_mode=2;before=plays;drive(city,"tree",22+city,3);assert(plays==before+2);
  native_mode=3;before=plays;drive(city,"tree",24+city,3);assert(plays==before+2);
 }
 native_mode=0;lookup_ok=0;before=plays;
 drive(0,"tree",30,3);assert(plays==before);unsigned lookup_before=lookups;
 drive(0,"tree",30,3);assert(lookups==lookup_before);
 lookup_ok=1;clock_ms+=400;drive(0,"tree",30,3);assert(plays==before+1);
 /* A conversion refusal leaves the tree solid and must still sound on its
  * actual contact. Its audio uses the car's world position, not world zero. */
 before=plays;drive(0,"tree",31,20);assert(plays==before+1);
 assert(!memcmp(last_position,position,sizeof position));
 assert(constructs==destructs&&constructs==lookups);
 puts("PASS: production contact/audio glue for Washington main tree sound and Paris event-only tree sound, 2-5m/s solid contacts, native success/failure, missing bank retry, unrelated/non-solid contacts, position and pre-solver speed");
 free(expected);VirtualFree(ram,0,MEM_RELEASE);return 0;
}
'''


def main():
    if not shutil.which('cl'):
        raise SystemExit('Run scripts/enter-env.ps1 first.')
    with tempfile.TemporaryDirectory(prefix='mm3-tree-audio-contact-') as temporary:
        directory = Path(temporary)
        code, executable = directory / 'contact.c', directory / 'contact.exe'
        code.write_text(PREFIX + INTEGRATION)
        subprocess.run(['cl', '/nologo', '/Od', '/MD', '/std:c11',
                        '/I' + str(ROOT / 'src'), '/I' + str(ROOT / 'tools/xboxrecomp/include'),
                        '/Fo' + str(directory / 'contact.obj'), '/Fe' + str(executable), str(code)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    main()
