/* Standalone incremental candidates. ALGORITHM: 1 MAYO,2 MQOM,3 SNOVA,4 UOV.
 * FLAGS 0 current; 1 layout/strategy preserving C clear; 3 layout without C clear.
 * MAYO additionally FLAGS 2 current batched helper, FLAGS 4 new rowwise no clear.
 * Every timed region includes final materialization and volatile scratch erase.
 * Native scalar comparison covers every output byte; RTL emits all bytes.
 */
#include <stdint.h>
#include <string.h>
#ifdef NATIVE_CHECK
#include <stdio.h>
#include "other_short_native.h"
#else
#include "../runtime/xmpk_asm.h"
extern void printstr(const char *);
extern void printhex(uint64_t);
extern void __attribute__((noreturn)) tohost_exit(uintptr_t);
#endif
#ifndef FLAGS
#define FLAGS 0
#endif
static inline uint64_t cycles(void) {
#ifdef NATIVE_CHECK
  return 0;
#else
  uint64_t v; asm volatile("rdcycle %0" : "=r"(v) :: "memory"); return v;
#endif
}
static uint64_t hw_cycles;
static uint64_t measured_qgemm(unsigned m,unsigned k,unsigned w,unsigned mode) {
  uint64_t t = cycles(), count = xmpk_qgemm_run(m,k,w,mode);
  hw_cycles += cycles() - t;
  return count;
}
static uint64_t measured_linxfm(unsigned d,unsigned n) {
  uint64_t t = cycles(), count = xmpk_linxfm_run(d,n);
  hw_cycles += cycles() - t;
  return count;
}
#define xmpk_qgemm_run measured_qgemm
#define xmpk_linxfm_run measured_linxfm
#include "../runtime/xmpk_asm.c"
#undef xmpk_qgemm_run
#undef xmpk_linxfm_run
static uint8_t a[65536] __attribute__((aligned(4096)));
static uint8_t b[131072] __attribute__((aligned(4096)));
static uint8_t out[32768] __attribute__((aligned(4096)));
static uint8_t ref[32768], serial[32768];
static size_t serial_used;
static uint64_t totals[2], compute[2], cleanup[2], preparation[2];
static uint32_t rng;
static unsigned pattern;
static uint8_t random_byte(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return pattern == 1 ? 0 : pattern == 2 ? 255 : (uint8_t)rng;
}
static void fill(uint8_t *p, size_t n, unsigned mask) {
  for (size_t i=0;i<n;i++) p[i]=random_byte() & mask;
}
static void fail(void) {
#ifdef NATIVE_CHECK
  abort();
#else
  printstr("OTHER_SHORT_FAILURE\n"); tohost_exit(0x3fffffff);
#endif
}
static void erase(void *p,size_t n) {
  static void *(*const volatile fn)(void *,int,size_t)=memset;
  fn(p,0,n);
}
/* Independent table reference derived from polynomial reduction. */
static uint8_t table16[16][16], table256[256][256];
static void make_tables(void) {
  for (unsigned bits=4;bits<=8;bits+=4)
    for(unsigned x=0;x<(1u<<bits);x++)
      for(unsigned y=0;y<(1u<<bits);y++) {
        unsigned p=0;
        for(unsigned i=0;i<bits;i++) if(y&(1u<<i)) p^=x<<i;
        for(int i=2*bits-2;i>=(int)bits;i--)
          if(p&(1u<<i)) p^=(bits==4?0x13:0x11b)<<(i-bits);
        if(bits==4) table16[x][y]=p; else table256[x][y]=p;
      }
}
static void finish(size_t bytes) {
#ifdef NATIVE_CHECK
  if(memcmp(out,ref,bytes)) fail();
#endif
  if(serial_used+bytes>sizeof(serial)) fail();
  memcpy(serial+serial_used,out,bytes); serial_used+=bytes;
}
static void mayo_candidate(unsigned rows,unsigned k,unsigned w,int clear) {
  unsigned aw=(k+15)/16;
  for(unsigned r=0;r<rows;r++)
    for(unsigned i=0;i<aw;i++) {
      uint64_t z=0;
      unsigned end=k-i*16; if(end>16) end=16;
      for(unsigned j=0;j<end;j++) z|=(uint64_t)(a[r*k+i*16+j]&15)<<(4*j);
      packed_gemm_left[r*aw+i]=z;
    }
  if(clear) memset(packed_gemm_result,0,rows*w*8);
  xmpk_qgemm_setup(packed_gemm_left,(const uint64_t *)(const void *)b,packed_gemm_result);
  if(measured_qgemm(rows,k,w,0)!=(uint64_t)rows*k*w*16) fail();
  for(unsigned i=0;i<rows*w;i++) ((uint64_t *)(void *)out)[i]^=packed_gemm_result[i];
}
static void mayo(unsigned level) {
  unsigned rows=level?12:10,k=level?142:80,w=level?108:40;
  fill(a,rows*k,15); fill(b,k*w*8,255); fill(out,rows*w*8,255);
#ifdef NATIVE_CHECK
  memcpy(ref,out,rows*w*8);
  for(unsigned r=0;r<rows;r++) for(unsigned c=0;c<w*16;c++) {
    uint8_t v=0;
    for(unsigned i=0;i<k;i++) v^=table16[a[r*k+i]][packed_get(b+i*w*8,c)];
    ref[r*w*8+c/2]^=v<<(4*(c&1));
  }
#endif
  hw_cycles=0; uint64_t start=cycles();
  if(FLAGS==0 || FLAGS==1 || FLAGS==4) {
    for(unsigned r=0;r<rows;r++) {
      if(FLAGS==0) {
        if(!xmpk_gf16_packed_gemm_add(a+r*k,0,1,(uint64_t *)(void *)b,w,
              (uint64_t *)(void *)(out+r*w*8),w,1,k,w)) fail();
      } else {
        /* Move only pointers; preserve per-row invocation count in A/B. */
        unsigned aw=(k+15)/16;
        for(unsigned i=0;i<aw;i++) {
          uint64_t z=0; unsigned end=k-i*16;if(end>16)end=16;
          for(unsigned j=0;j<end;j++)z|=(uint64_t)(a[r*k+i*16+j]&15)<<(4*j);
          packed_gemm_left[i]=z;
        }
        if(FLAGS==1) memset(packed_gemm_result,0,w*8);
        xmpk_qgemm_setup(packed_gemm_left,(uint64_t *)(void *)b,packed_gemm_result);
        if(measured_qgemm(1,k,w,0)!=(uint64_t)k*w*16)fail();
        for(unsigned i=0;i<w;i++)((uint64_t *)(void *)out)[r*w+i]^=packed_gemm_result[i];
      }
    }
  } else if(FLAGS==2) {
    if(!xmpk_gf16_packed_gemm_add(a,k,1,(uint64_t *)(void *)b,w,
         (uint64_t *)(void *)out,w,rows,k,w))fail();
  } else mayo_candidate(rows,k,w,0);
  uint64_t t=cycles(); compute[level]=hw_cycles;
  /* Fixed scratch address ranges erased for every variant, even zero-copy. */
  erase(packed_gemm_left,rows*((k+15)/16)*8);
  erase(packed_gemm_right,k*w*8); erase(packed_gemm_result,rows*w*8);
  cleanup[level]=cycles()-t; totals[level]=cycles()-start; finish(rows*w*8);
}
static void mqom(unsigned level) {
  unsigned n=level?128:64; fill(a,3*n*n,255); fill(b,n,255);
#ifdef NATIVE_CHECK
  for(unsigned item=0;item<3;item++)for(unsigned r=0;r<n;r++){
    uint8_t v=0;for(unsigned c=0;c<=r;c++)v^=table256[a[(item*n+r)*n+c]][b[c]];
    ref[item*n+r]=v;
  }
#endif
  hw_cycles=0;uint64_t start=cycles();
  if(FLAGS==0) {
    if(!xmpk_gf256_matvec_batch(a,n*n,b,out,n,n,2,3)) fail();
  } else {
    for(unsigned item=0;item<3;item++)for(unsigned r=0;r<n;r++){
      uint64_t z=0;
      for(unsigned c=0;c<=r;c+=8) {
        uint64_t x,y;memcpy(&x,a+(item*n+r)*n+c,8);memcpy(&y,b+c,8);
        uint64_t p=xmpk_gf256_mul8(x,y);
        unsigned active=r-c+1;if(active<8)p&=(UINT64_C(1)<<(8*active))-1;
        z^=p;
      }
      z^=z>>32;z^=z>>16;z^=z>>8;out[item*n+r]=(uint8_t)z;
    }
  }
  uint64_t t=cycles();compute[level]=hw_cycles;
  erase(matvec_matrix,3*n*n);erase(matvec_vector,n*8);erase(matvec_result,3*n*8);
  cleanup[level]=cycles()-t;totals[level]=cycles()-start;finish(3*n);
}
static void snova(unsigned level) {
  unsigned d=level?5:4,cols=d,batch=d,apps=24,rs=d*cols;
  fill(a,apps*d*d,15);fill(b,batch*rs,15);fill(out,apps*batch*rs,15);
#ifdef NATIVE_CHECK
  memcpy(ref,out,apps*batch*rs);
  for(unsigned app=0;app<apps;app++)for(unsigned item=0;item<batch;item++)
    for(unsigned r=0;r<d;r++)for(unsigned c=0;c<cols;c++)
      for(unsigned i=0;i<d;i++)ref[(app*batch+item)*rs+r*cols+c]^=
         table16[a[app*d*d+r*d+i]][b[item*rs+i*cols+c]];
#endif
  hw_cycles=0;uint64_t start=cycles();
  if(FLAGS==0) {
    if(!xmpk_gf16_linxfm_prepare_right_batch(b,rs,d,cols,batch))fail();
  } else {
    for(unsigned item=0;item<batch;item++)for(unsigned c=0;c<cols;c++){
      uint64_t z=0;for(unsigned r=0;r<d;r++)z|=(uint64_t)b[item*rs+r*cols+c]<<(4*r);
      linxfm_input[item*cols+c]=z;
    }
  }
  preparation[level]=cycles()-start;
  for(unsigned app=0;app<apps;app++) {
    if(FLAGS==0) {
      if(!xmpk_gf16_linxfm_apply_prepared(a+app*d*d,out+app*batch*rs,rs,1))fail();
    } else {
      for(unsigned r=0;r<d;r++){
        uint64_t z=0;for(unsigned c=0;c<d;c++)z|=(uint64_t)a[app*d*d+r*d+c]<<(4*c);
        linxfm_transform[r]=z;
      }
      if(FLAGS==1)memset(linxfm_output,0,batch*cols*8);
      xmpk_linxfm_setup(linxfm_transform,linxfm_input,linxfm_output);
      if(measured_linxfm(d,batch*cols)!=(uint64_t)d*batch*cols)fail();
      for(unsigned item=0;item<batch;item++)for(unsigned c=0;c<cols;c++){
        uint64_t z=linxfm_output[item*cols+c];
        for(unsigned r=0;r<d;r++){out[(app*batch+item)*rs+r*cols+c]^=z&15;z>>=4;}
      }
    }
  }
  uint64_t t=cycles();compute[level]=hw_cycles;
  erase(linxfm_transform,d*8);erase(linxfm_input,batch*cols*8);erase(linxfm_output,batch*cols*8);
  linxfm_prepared_dimension=linxfm_prepared_cols=linxfm_prepared_batch=0;
  cleanup[level]=cycles()-t;totals[level]=cycles()-start;finish(apps*batch*rs);
}
static uint64_t load_window(const uint8_t *src,unsigned stride,unsigned index,unsigned count) {
  unsigned byte=index/2; uint64_t lo=0,hi=0;
  unsigned avail=stride-byte,first=avail<8?avail:8;
  memcpy(&lo,src+byte,first);
  if((index&1)&&avail>8)memcpy(&hi,src+byte+8,1);
  uint64_t z=(index&1)?(lo>>4)|(hi<<60):lo;
  if(count<16)z&=(UINT64_C(1)<<(4*count))-1;
  return z;
}
static void uov(unsigned level) {
  unsigned offset=level?3:0,k=96-offset,rows=64,w=4,stride=48;
#ifdef NATIVE_CHECK
  /* Extra offset/tail cases are selected through seed, no out-of-bounds reads. */
  if(pattern>=3) {offset=pattern-3;k=96-offset;}
#endif
  fill(a,rows*stride,255);fill(b,k*w*8,255);fill(out,rows*w*8,255);
#ifdef NATIVE_CHECK
  memcpy(ref,out,rows*w*8);
  for(unsigned r=0;r<rows;r++)for(unsigned c=0;c<w*16;c++){
    uint8_t v=0;for(unsigned i=0;i<k;i++)v^=table16[packed_get(a+r*stride,offset+i)][packed_get(b+i*w*8,c)];
    ref[r*w*8+c/2]^=v<<(4*(c&1));
  }
#endif
  hw_cycles=0;uint64_t start=cycles();
  if(FLAGS==0) {
    if(!xmpk_gf16_batch_matvec_add(a,stride,offset,b,out,rows,k,w*8))fail();
  } else {
    unsigned aw=(k+15)/16;
    for(unsigned r=0;r<rows;r++)for(unsigned i=0;i<aw;i++){
      unsigned count=k-i*16;if(count>16)count=16;
      batch_vec_left[r*aw+i]=load_window(a+r*stride,stride,offset+i*16,count);
    }
    if(FLAGS==1)memset(batch_vec_result,0,rows*w*8);
    xmpk_qgemm_setup(batch_vec_left,(uint64_t *)(void *)b,batch_vec_result);
    if(measured_qgemm(rows,k,w,0)!=(uint64_t)rows*k*w*16)fail();
    for(unsigned i=0;i<rows*w;i++){
      uint64_t z;memcpy(&z,out+i*8,8);z^=batch_vec_result[i];memcpy(out+i*8,&z,8);
    }
  }
  uint64_t t=cycles();compute[level]=hw_cycles;
  erase(batch_vec_left,rows*((k+15)/16)*8);erase(batch_vec_right,k*w*8);
  erase(batch_vec_result,rows*w*8);
  cleanup[level]=cycles()-t;totals[level]=cycles()-start;finish(rows*w*8);
}
static void run(unsigned seed,unsigned mode) {
  rng=seed;pattern=mode;serial_used=0;
  /* Dirty result scratch proves that overwrite semantics permit removing clear. */
  memset(packed_gemm_result,0xa5,sizeof(packed_gemm_result));
  memset(batch_vec_result,0xa5,sizeof(batch_vec_result));
  memset(linxfm_output,0xa5,sizeof(linxfm_output));
  for(unsigned level=0;level<2;level++){
#if ALGORITHM == 1
    mayo(level);
#elif ALGORITHM == 2
    mqom(level);
#elif ALGORITHM == 3
    snova(level);
#elif ALGORITHM == 4
    uov(level);
#endif
  }
}
#ifdef NATIVE_CHECK
int main(int argc,char **argv) {
  make_tables();
  for(unsigned seed=1;seed<=12;seed++)run(seed,seed<=2?seed:0);
#if ALGORITHM == 4
  for(unsigned offset=0;offset<32;offset++)run(97+offset,3+offset);
#endif
  run(0x12345678,0);
  if(argc>1){FILE *f=fopen(argv[1],"wb");if(!f)fail();fwrite(serial,1,serial_used,f);fclose(f);}
  printf("NATIVE_FULL_OUTPUT_PASS algorithm=%u flags=%u bytes=%zu\n",ALGORITHM,FLAGS,serial_used);
  return 0;
}
#else
/* The local HTIF service refreshes fromhost cache slots in every way.
 * Freeze all metrics and bytes before the first host request: later reads from
 * unrelated cache lines sharing those slots would otherwise see the ACK value.
 * One write also avoids thousands of host round trips outside the timed region.
 */
static char text_output[68000] __attribute__((aligned(4096)));
static size_t text_used;
static const char hex[]="0123456789abcdef";
static void append(const char *s) {
  while(*s) {
    if(text_used+1>=sizeof(text_output)) fail();
    text_output[text_used++]=*s++;
  }
}
static void metric(const char *name,uint64_t value) {
  append(name);append("=0x");
  for(int i=15;i>=0;i--)text_output[text_used++]=hex[(value>>(4*i))&15];
  append("\n");
}
int main(void) {
  run(0x12345678,0);
  for(unsigned i=0;i<2;i++)
    if(totals[i]<compute[i]+cleanup[i])fail();
  metric("eq_base",ALGORITHM);metric("eq_count",2);metric("flags",FLAGS);
  metric("total_l1",totals[0]);metric("total_l5",totals[1]);
  metric("compute_l1",compute[0]);metric("compute_l5",compute[1]);
  metric("cleanup_l1",cleanup[0]);metric("cleanup_l5",cleanup[1]);
  metric("prepare_l1",preparation[0]);metric("prepare_l5",preparation[1]);
  append("SERIALIZED_HEX=");
  for(size_t i=0;i<serial_used;i++){
    text_output[text_used++]=hex[serial[i]>>4];
    text_output[text_used++]=hex[serial[i]&15];
  }
  append("\nOTHER_SHORT_SUCCESS\n");text_output[text_used]=0;
  printstr(text_output);tohost_exit(0);
}
#endif
