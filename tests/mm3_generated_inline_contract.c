/* Compile the identical production kernels in two translation units, /Ob1
 * and /Ob2. Compare their complete guest state and writable fixture memory,
 * then interleave timing pairs in one process. No game process is accessed. */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#define RECOMP_GENERATED_CODE
#include "mm3_recomp_types.h"
#include "kernels.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp,g_df;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;

static unsigned char memory[0x400000], initial[8192], expected_memory[8192];
static uint32_t rng=0x12345678;
static uint32_t random_u32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float random_float(void) {return ((int)(random_u32()%200001)-100000)/257.0f;}
typedef struct {
 uint32_t ax,cx,dx,sp,bx,si,di,bp,seh;
 double fp[8]; int top,cmp,df; uint16_t cc,cw;
 RecompXmm xmm[8]; unsigned mxcsr;
} State;
static State snapshot(void) {
 State s;memset(&s,0,sizeof s);
 s.ax=g_eax;s.cx=g_ecx;s.dx=g_edx;s.sp=g_esp;s.bx=g_ebx;s.si=g_esi;s.di=g_edi;
 s.bp=g_ebp;s.seh=g_seh_ebp;memcpy(s.fp,g_fp_stack,sizeof s.fp);
 s.top=g_fp_top;s.cmp=g_fp_cmp;s.df=g_df;s.cc=g_fp_cc;s.cw=g_fp_control_word;
 s.xmm[0]=g_xmm0;s.xmm[1]=g_xmm1;s.xmm[2]=g_xmm2;s.xmm[3]=g_xmm3;
 s.xmm[4]=g_xmm4;s.xmm[5]=g_xmm5;s.xmm[6]=g_xmm6;s.xmm[7]=g_xmm7;
 s.mxcsr=_mm_getcsr();return s;
}
static void restore(State s) {
 g_eax=s.ax;g_ecx=s.cx;g_edx=s.dx;g_esp=s.sp;g_ebx=s.bx;g_esi=s.si;g_edi=s.di;
 g_ebp=s.bp;g_seh_ebp=s.seh;memcpy(g_fp_stack,s.fp,sizeof s.fp);
 g_fp_top=s.top;g_fp_cmp=s.cmp;g_df=s.df;g_fp_cc=s.cc;g_fp_control_word=s.cw;
 g_xmm0=s.xmm[0];g_xmm1=s.xmm[1];g_xmm2=s.xmm[2];g_xmm3=s.xmm[3];
 g_xmm4=s.xmm[4];g_xmm5=s.xmm[5];g_xmm6=s.xmm[6];g_xmm7=s.xmm[7];
 _mm_setcsr(s.mxcsr);
}
static void arguments(unsigned kernel,unsigned test) {
 g_esp=4096;MEM32(4096)=0x600000;
 if(kernel==0) { /* Native point -> homogeneous matrix transform chain. */
  MEM32(4100)=test%3==1?512:1024;MEM32(4104)=512;MEM32(4108)=768;
 } else if(kernel==1) {
  MEMF(4100)=test%2? -11.25f:13.75f;
 } else if(kernel==2) {
  g_eax=512;g_edx=1280;g_ecx=1536;
  MEM32(4100)=1024;MEMF(4104)=0.75f;MEMF(4108)=1.25f;
 } else if(kernel==5) {
  g_ecx=512;g_edx=768;MEM32(4100)=test%25;
 } else {
  uint32_t out=1024,left=512,right=768;
  if(test%5==1)out=left;
  if(test%5==2)out=right;
  if(test%5==3)out=left=right;
  if(test%5==4)out=left+4;
  MEM32(4100)=out;MEM32(4104)=left;MEM32(4108)=right;
 }
}
static void seed(unsigned kernel,unsigned test,unsigned initial_mxcsr) {
 unsigned n;
 for(n=0;n<8192;++n)memory[n]=(unsigned char)random_u32();
 for(n=448;n<2048;n+=4)MEMF(n)=random_float();
 g_eax=random_u32();g_ecx=random_u32();g_edx=random_u32();
 g_ebx=random_u32();g_esi=random_u32();g_edi=random_u32();
 g_ebp=random_u32();g_seh_ebp=random_u32();g_fp_top=test%8;
 g_fp_cmp=(int)(test%4)-1;g_fp_cc=(uint16_t)random_u32();g_df=(test/8)&1;
 g_fp_control_word=(uint16_t)(0x3f+((test/8)%16)*0x100);
 for(n=0;n<8;++n)g_fp_stack[n]=random_float();
 memset(&g_xmm0,0x15,sizeof g_xmm0);memset(&g_xmm1,0x25,sizeof g_xmm1);
 memset(&g_xmm2,0x35,sizeof g_xmm2);memset(&g_xmm3,0x45,sizeof g_xmm3);
 memset(&g_xmm4,0x55,sizeof g_xmm4);memset(&g_xmm5,0x65,sizeof g_xmm5);
 memset(&g_xmm6,0x75,sizeof g_xmm6);memset(&g_xmm7,0x85,sizeof g_xmm7);
 if(test%17==0) {
  static const uint32_t bits[]={0,0x80000000,1,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa12345};
  MEM32(512+(test%12)*4)=bits[(test/17)%9];
  MEM32(768+(test%16)*4)=bits[(test/153)%9];
 }
 if(kernel==0 && test%2==0) {MEMF(780)=MEMF(796)=MEMF(812)=0;MEMF(828)=1;}
 arguments(kernel,test);
 _mm_setcsr((initial_mxcsr&~(0x6000u|0x8040u|0x3fu))|
  (((test/128)%4)<<13)|(((test/512)%2)<<15)|(((test/1024)%2)<<6));
}
static double benchmark(void (*function)(void),unsigned kernel,unsigned test) {
 LARGE_INTEGER a,b,f;unsigned n;
 QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);
 for(n=0;n<500000;++n) {arguments(kernel,test);function();}
 QueryPerformanceCounter(&b);
 return (b.QuadPart-a.QuadPart)*1e9/(double)f.QuadPart/500000;
}
int main(void) {
 unsigned kernel,test,n,initial_mxcsr=_mm_getcsr();
 void (*one[])(void)={ob1_sub_002598CD,ob1_sub_00012D29,ob1_sub_00012D9A,
  ob1_sub_00259982,ob1_matrix_native,ob1_sub_001C793A};
 void (*two[])(void)={ob2_sub_002598CD,ob2_sub_00012D29,ob2_sub_00012D9A,
  ob2_sub_00259982,ob2_matrix_native,ob2_sub_001C793A};
 const char *names[]={"point_transform_chain","floor","packed_vector","matrix_generated","matrix_native","visibility_generated"};
 g_xbox_mem_offset=(ptrdiff_t)memory;
 #include "constants.inc"
 for(kernel=0;kernel<6;++kernel) {
  for(test=0;test<20000;++test) {
   State start,expected,actual;
   seed(kernel,test,initial_mxcsr);start=snapshot();memcpy(initial,memory,sizeof initial);
   one[kernel]();expected=snapshot();memcpy(expected_memory,memory,sizeof expected_memory);
   restore(start);memcpy(memory,initial,sizeof initial);two[kernel]();actual=snapshot();
   if(memcmp(&expected,&actual,sizeof expected)||memcmp(expected_memory,memory,sizeof expected_memory)) {
    printf("FAIL %s test%u\n",names[kernel],test);
    for(n=0;n<sizeof expected;++n)if(((unsigned char*)&expected)[n]!=((unsigned char*)&actual)[n])
     printf("state byte%u %02x != %02x\n",n,((unsigned char*)&expected)[n],((unsigned char*)&actual)[n]);
    return 1;
   }
  }
  printf("PASS %s: 20000 exact state/memory/MXCSR cases\n",names[kernel]);fflush(stdout);
 }
 _mm_setcsr(initial_mxcsr);
 for(kernel=0;kernel<6;++kernel) {
  for(test=0;test<9;++test) {
   double first,second;
   seed(kernel,0,initial_mxcsr);_mm_setcsr(initial_mxcsr);g_fp_control_word=0x23f;g_df=0;
   MEMF(780)=MEMF(796)=MEMF(812)=0;MEMF(828)=test<4?1:2;
   if(test&1) {second=benchmark(two[kernel],kernel,0);first=benchmark(one[kernel],kernel,0);}
   else {first=benchmark(one[kernel],kernel,0);second=benchmark(two[kernel],kernel,0);}
   printf("BENCH %s %u %.3f %.3f %.5f\n",names[kernel],test,first,second,first/second);fflush(stdout);
  }
 }
 _mm_setcsr(initial_mxcsr);return 0;
}
