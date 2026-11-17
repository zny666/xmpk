#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <random>
#include <stdexcept>
#include <string>
#include <algorithm>
#include "verilated.h"
#include HEADER
using DUT = DUT_CLASS;
static DUT d;
static std::vector<uint8_t> mem(1<<20);
static uint64_t ticks, reads, writes, b_reads, due, pending_data;
static bool pending, pending_error;
static int stall, fault_at=-1;
static unsigned selected_opcode;
static bool fault_write;
static std::mt19937 rng(0x225613);
static void check(bool c,const char *s){if(!c)throw std::runtime_error(s);}
static uint64_t load(uint64_t addr) {
  check(addr+8<=mem.size(),"memory out of bounds");
  uint64_t v=0;for(int j=0;j<8;j++)v|=uint64_t(mem[addr+j])<<(8*j);return v;
}
static void store(uint64_t addr,uint64_t v) {
  check(addr+8<=mem.size(),"store out of bounds");
  for(int j=0;j<8;j++)mem[addr+j]=v>>(8*j);
}
static void tick() {
  d.clk_i=0;
  d.mem_req_ready_i=!stall||ticks%5!=0;
  d.mem_rsp_valid_i=pending&&ticks>=due;
  d.mem_rsp_data_i=pending_data;d.mem_rsp_error_i=pending_error;
  if(d.mem_rsp_valid_i)pending=false;
  d.eval();
  bool accept=d.mem_req_valid_o&&d.mem_req_ready_i;
  auto addr=d.mem_req_addr_o,data=d.mem_req_wdata_o;
  bool wr=d.mem_req_write_o;
  d.clk_i=1;d.eval();
  if(accept){
    check(!pending,"multiple outstanding");
    check((addr&7)==0,"unaligned request");
    pending_error=(int(reads+writes)==fault_at)||(fault_write&&wr);
    pending_data=wr?0:load(addr);
    if(wr){writes++;if(!pending_error)store(addr,data);}
    else {reads++;if(addr>=0x20000&&addr<0x50000)b_reads++;}
    pending=true;due=ticks+1+(stall?3:0);
  }
  ticks++;
}
static void issue(bool run,uint64_t a,uint64_t b,uint64_t c=0) {
  unsigned count=0;
  while(!d.cmd_ready_o){tick();check(++count<2000000,"issue timeout");}
  d.cmd_run_i=run;d.a_i=a;d.b_i=b;
  d.cmd_opcode_i=selected_opcode;
#ifndef SOLVE
  d.c_i=c;
#endif
  d.cmd_valid_i=1;tick();d.cmd_valid_i=0;
}
static uint64_t wait() {
  unsigned count=0;while(!d.rsp_valid_o){tick();check(++count<2000000,"timeout");}
  uint64_t r=d.rsp_data_o;tick();return r;
}
static int gf(int a,int b,int bits) {
  int p=bits==4?0x13:0x11b,r=0;
  for(int i=0;i<bits;i++){if(b&1)r^=a;b>>=1;a<<=1;if(a&(1<<bits))a^=p;}
  return r;
}
static void metrics(const char*kind,int m,int k,int n,int mode,uint64_t t,uint64_t r,uint64_t w,uint64_t br) {
  printf("%s m=%d k=%d n=%d mode=%d stall=%d cycles=%llu reads=%llu writes=%llu b_reads=%llu PASS\n",
    kind,m,k,n,mode,stall,(unsigned long long)(ticks-t),(unsigned long long)(reads-r),
    (unsigned long long)(writes-w),(unsigned long long)(b_reads-br));
}
#ifdef EXT
static void ext(int m,int k,int n,int l,bool packed,bool maximum=false) {
  std::fill(mem.begin(),mem.end(),0xA5);
  int stride=packed?3:16;
  std::vector<int>A(m*k*l),B(k*n*l);
  for(int &v:A)v=maximum?126:rng()%127;
  for(int &v:B)v=maximum?126:rng()%127;
  for(int i=0;i<m*k;i++)for(int j=0;j<l;j++)mem[0x1000+i*stride+j]=A[i*l+j];
  for(int i=0;i<k*n;i++)for(int j=0;j<l;j++)mem[0x20000+i*stride+j]=B[i*l+j];
  issue(0,0x1000,0x20000,0x60000);
  auto t=ticks,r=reads,w=writes,br=b_reads;
  issue(1,m|(k<<8)|(n<<16)|(uint64_t(l==10)<<24)|(uint64_t(packed)<<25),0);
  auto result=wait();check(result==uint64_t(m)*k*n*l*l,"ext response count");
  for(int i=0;i<m;i++)for(int j=0;j<n;j++) {
    int64_t acc[19]={0};
    for(int h=0;h<k;h++)for(int u=0;u<l;u++)for(int v=0;v<l;v++)
      acc[u+v]+=A[(i*k+h)*l+u]*B[(h*n+j)*l+v];
    for(int h=2*l-2;h>=l;h--){acc[h-l]+=acc[h];acc[h-l+(l==10?3:1)]+=acc[h]*(l==10?2:1);}
    for(int u=0;u<l;u++)check(mem[0x60000+(i*n+j)*16+u]==acc[u]%127,"ext oracle mismatch");
    for(int u=l;u<(l==10?16:8);u++)check(mem[0x60000+(i*n+j)*16+u]==0,"ext output padding");
  }
  metrics(packed?"EXT_PACKED":"EXT",m,k,n,l,t,r,w,br);
}
#endif
#ifdef Q
static void qtest(int m,int k,int n,int mode) {
  std::fill(mem.begin(),mem.end(),0);
  int bits=mode==0?4:8,lanes=64/bits,aw=(k+lanes-1)/lanes,p=mode==2?7:mode==3?31:127;
  int bound=mode<2?1<<bits:p;
  std::vector<int>A(m*k),B(k*n*lanes);
  for(int &v:A)v=rng()%bound;for(int &v:B)v=rng()%bound;
  for(int i=0;i<m;i++)for(int h=0;h<k;h++) {
    auto addr=0x1000+(i*aw+h/lanes)*8;
    store(addr,load(addr)|(uint64_t(A[i*k+h])<<(bits*(h%lanes))));
  }
  for(int h=0;h<k;h++)for(int j=0;j<n;j++)for(int v=0;v<lanes;v++) {
    auto addr=0x20000+(h*n+j)*8;
    store(addr,load(addr)|(uint64_t(B[(h*n+j)*lanes+v])<<(bits*v)));
  }
  issue(0,0x1000,0x20000,0x60000);
  auto t=ticks,r=reads,w=writes,br=b_reads;
  issue(1,m|(k<<8)|(n<<16)|(uint64_t(mode)<<28),0);
  check(wait()==uint64_t(m)*k*n*lanes,"q response count");
  for(int i=0;i<m;i++)for(int j=0;j<n;j++)for(int v=0;v<lanes;v++){
    int acc=0;
    for(int h=0;h<k;h++){
      int a=A[i*k+h],b=B[(h*n+j)*lanes+v];
      if(mode<2)acc^=gf(a,b,bits);else acc=(acc+a*b)%p;
    }
    check(((load(0x60000+(i*n+j)*8)>>(bits*v))&(bound-1+(mode>=2?((1<<bits)-bound):0)))==uint64_t(acc),"q oracle mismatch");
  }
  metrics("Q",m,k,n,mode,t,r,w,br);
}
#endif
#ifdef SOLVE
static void setlane(int row,int col,int words,int bits,int value){
  auto addr=0x1000+(row*words+col/(64/bits))*8;
  uint64_t mask=(uint64_t(1)<<bits)-1;
  int shift=bits*(col%(64/bits));
  store(addr,(load(addr)&~(mask<<shift))|(uint64_t(value)<<shift));
}
static void solve(int n,int bits,int kind) {
  std::fill(mem.begin(),mem.end(),0);
  int bound=1<<bits,lanes=64/bits,words=(n+lanes)/lanes;
  std::vector<int>A(n*n),x(n),rhs(n);
  for(int i=0;i<n;i++){A[i*n+i]=1;x[i]=rng()%bound;}
  if(kind!=0){
    // Invertible by construction; row ops exercise dense factors and pivot swaps.
    for(int z=0;z<8*n;z++){
      int i=rng()%n,j=rng()%n,f=1+rng()%(bound-1);
      if(i!=j)for(int k=0;k<n;k++)A[i*n+k]^=gf(A[j*n+k],f,bits);
    }
    for(int z=0;z<n;z++){int j=rng()%n;for(int k=0;k<n;k++)std::swap(A[z*n+k],A[j*n+k]);}
  }
  if(kind==2)for(int j=0;j<n;j++)A[(n-1)*n+j]=0;
  for(int i=0;i<n;i++){
    for(int j=0;j<n;j++){rhs[i]^=gf(A[i*n+j],x[j],bits);setlane(i,j,words,bits,A[i*n+j]);}
    setlane(i,n,words,bits,rhs[i]);
  }
  issue(0,0x1000,0x60000);
  auto t=ticks,r=reads,w=writes,br=b_reads;
  issue(1,n|(bits==8?0x100:0),0);
  auto result=wait();
  if(kind==2)check(result>>63,"singular accepted");
  else {
    check(result==uint64_t(n),"solve response");
    for(int i=0;i<n;i++)check(((load(0x60000+(i/lanes)*8)>>(bits*(i%lanes)))&(bound-1))==uint64_t(x[i]),"solve oracle mismatch");
  }
  metrics(kind==2?"SINGULAR":kind?"SOLVE_DENSE":"SOLVE_ID",n,words,0,bits,t,r,w,br);
}
#endif
#ifdef MQ
static void mqtest(int n,int m) {
  std::fill(mem.begin(),mem.end(),0);
  int terms=n*(n+1)/2, words=(terms+15)/16;
  std::vector<int>x(n),q(m*terms);
  for(int &v:x)v=rng()%16;
  for(int &v:q)v=rng()%16;
  for(int i=0;i<n;i++){
    auto addr=0x20000+(i/16)*8;
    store(addr,load(addr)|(uint64_t(x[i])<<(4*(i%16))));
  }
  for(int e=0;e<m;e++)for(int t=0;t<terms;t++){
    auto addr=0x1000+(e*words+t/16)*8;
    store(addr,load(addr)|(uint64_t(q[e*terms+t])<<(4*(t%16))));
  }
  issue(0,0x1000,0x20000,0x60000);
  auto t=ticks,r=reads,w=writes,br=b_reads;
  issue(1,n|(m<<8),0);
  check(wait()==uint64_t(m)*terms,"mq count");
  for(int e=0;e<m;e++){
    int acc=0,index=0;
    for(int i=0;i<n;i++)for(int j=i;j<n;j++)
      acc^=gf(q[e*terms+index++],gf(x[i],x[j],4),4);
    check(((load(0x60000+(e/16)*8)>>(4*(e%16)))&15)==uint64_t(acc),"mq oracle");
  }
  if(m%16)check(load(0x60000+(m/16)*8)>>(4*(m%16))==0,"mq padding");
  metrics("MQ",m,n,0,4,t,r,w,br);
}
#endif
#ifdef LIN
static void lintest(int l,int count) {
  std::fill(mem.begin(),mem.end(),0);
  std::vector<int>a(l*l),x(count*l);
  for(int &v:a)v=rng()%16;
  for(int &v:x)v=rng()%16;
  for(int i=0;i<l;i++)for(int j=0;j<l;j++){
    auto addr=0x1000+i*8;
    store(addr,load(addr)|(uint64_t(a[i*l+j])<<(4*j)));
  }
  for(int v=0;v<count;v++)for(int j=0;j<l;j++){
    auto addr=0x20000+v*8;
    store(addr,load(addr)|(uint64_t(x[v*l+j])<<(4*j)));
  }
  issue(0,0x1000,0x20000,0x60000);
  auto t=ticks,r=reads,w=writes,br=b_reads;
  issue(1,l|(count<<8),0);check(wait()==uint64_t(l)*count,"lin count");
  for(int v=0;v<count;v++){
    for(int i=0;i<l;i++){
      int acc=0;for(int j=0;j<l;j++)acc^=gf(a[i*l+j],x[v*l+j],4);
      check(((load(0x60000+v*8)>>(4*i))&15)==uint64_t(acc),"lin oracle");
    }
    if(l<16)check(load(0x60000+v*8)>>(4*l)==0,"lin padding");
  }
  metrics("LIN",count,l,0,4,t,r,w,br);
}
#endif
#ifdef ARITH
static uint64_t command(unsigned op,uint64_t a,uint64_t b=0,uint64_t c=0) {
  selected_opcode=op;issue(0,a,b,c);return wait();
}
static uint64_t packed_product(uint64_t a,uint64_t b,int bits,int modulus=0) {
  uint64_t out=0,mask=(uint64_t(1)<<bits)-1;
  for(int i=0;i<64/bits;i++){
    int x=(a>>(bits*i))&mask,y=(b>>(bits*i))&mask;
    out|=uint64_t(modulus?(x*y)%modulus:gf(x,y,bits))<<(bits*i);
  }
  return out;
}
static void arithmetic() {
  for(int z=0;z<128;z++){
    uint64_t a=(uint64_t(rng())<<32)|rng(),b=(uint64_t(rng())<<32)|rng(),c=(uint64_t(rng())<<32)|rng();
    if(z==0)a=b=c=0;
    if(z==1)a=b=c=~uint64_t(0);
    auto p=packed_product(a,b,4);
    check(command(10,a,b)==p,"gf16 mul");
    check(command(11,a,b,c)==(p^c),"gf16 macc old rd");
    check(command(12,a,b)==packed_product(a,b,8),"gf256 mul");
    uint64_t prime_a=0,prime_b=0;
    for(int i=0;i<8;i++){prime_a|=uint64_t(rng()%127)<<(8*i);prime_b|=uint64_t(rng()%127)<<(8*i);}
    check(command(13,prime_a,prime_b)==packed_product(prime_a,prime_b,8,127),"gf127 mul");
    uint64_t inv=0;
    for(int i=0;i<8;i++){
      int x=(a>>(8*i))&255,v=0;
      if(x)for(int j=1;j<256;j++)if(gf(x,j,8)==1){v=j;break;}
      inv|=uint64_t(v)<<(8*i);
    }
    check(command(20,a)==inv,"gf256 inv");
    for(int bits:{4,8}){
      int lanes=64/bits,mask=(1<<bits)-1,index=z%lanes,fold=0,first=lanes;
      uint64_t broad=0;
      for(int i=0;i<lanes;i++){
        int x=(a>>(bits*i))&mask;fold^=x;if(x&&first==lanes)first=i;
        broad|=((a>>(bits*index))&mask)<<(bits*i);
      }
      check(command(bits==4?16:17,a,index)==broad,"broadcast");
      check(command(bits==4?18:19,a)==uint64_t(fold),"xor fold");
      check(command(bits==4?21:22,a)==uint64_t(first),"find nonzero");
      if(bits==4){
        int val=(a>>(4*index))&15,iv=0;
        if(val)for(int j=1;j<16;j++)if(gf(val,j,4)==1){iv=j;break;}
        check(command(34,a,index)==uint64_t((first<<8)|(val<<4)|iv),"pivot");
      }
    }
    int dot=0;for(int i=0;i<16;i++)dot^=(p>>(4*i))&15;
    check(command(33,a,b)==uint64_t(dot),"dot");
    check(command(35,a,b,c)==(c^packed_product(a,(b&15)*0x1111111111111111ULL,4)),"rowelim");
  }
  puts("ARITHMETIC_RANDOM_BOUNDARY_PASS");
}
static void tile_tests() {
  auto clear=[](){for(int i=0;i<8;i++)command(23,0,8+i);};
  uint64_t word=0x123456789abcdef0ULL;
  for(int i=0;i<8;i++){command(23,word^i,i);check(command(24,0,i)==(word^i),"B TRF");}
  for(int i=0;i<8;i++){command(23,word^i,8+i);check(command(24,0,8+i)==(word^i),"C TRF");}
  command(23,word,0);clear();
  check(command(25,2,0)==1,"ger16 count");
  check(command(24,0,8)==packed_product(word,0x2222222222222222ULL,4),"ger16 result");
  clear();check(command(26,2,0)==1,"ger16r count");
  check(command(24,0,8)==packed_product(word,0x2222222222222222ULL,4),"ger16r result");
  clear();check(command(27,word,0,2)==1,"bger16r count");
  check(command(24,0,8)==packed_product(word,0x2222222222222222ULL,4),"bger16r result");
  command(28,2,0);clear();check(command(29,word,0)==1,"ager16r count");
  check(command(24,0,8)==packed_product(word,0x2222222222222222ULL,4),"ager16r result");
  command(30,2,0);clear();check(command(31,word,0)==1,"ager16x16 count");
  check(command(24,0,8)==packed_product(word,0x2222222222222222ULL,4),"ager16x16 result");
  clear();check(command(32,word,0,word)==2,"ager16r2 count");
  check(command(24,0,8)==packed_product(word,0x2222222222222222ULL,4),"ager16r2 first");
  check(command(24,0,9)==packed_product(word,0x2222222222222222ULL,4),"ager16r2 second");
  check(command(38,0x70000,word)==0,"xmem status");check(load(0x70000)==word,"xmem store");
  puts("TILE_GER_AND_STORE_PASS");
}
static void auto_ger(int n) {
  std::fill(mem.begin(),mem.end(),0);
  int words=n/16;
  std::vector<int>a(n*n),b(n*n);
  for(int &v:a)v=rng()%16;
  for(int &v:b)v=rng()%16;
  for(int i=0;i<n;i++)for(int j=0;j<n;j++){
    auto aa=0x1000+(i*words+j/16)*8,ba=0x20000+(i*words+j/16)*8;
    store(aa,load(aa)|(uint64_t(a[i*n+j])<<(4*(j%16))));
    store(ba,load(ba)|(uint64_t(b[i*n+j])<<(4*(j%16))));
  }
  command(36,0x1000,0x20000);
  check(command(37,n)==uint64_t(n*n),"auto GER count");
  for(int i=0;i<n;i++)for(int w=0;w<words;w++){
    int linear=i*words+w,addr=8+(linear%8)+((linear/8)<<4);
    uint64_t actual=command(24,0,addr),expected=0;
    for(int lane=0;lane<16;lane++){
      int acc=0;
      for(int k=0;k<n;k++)acc^=gf(a[i*n+k],b[k*n+w*16+lane],4);
      expected|=uint64_t(acc)<<(4*lane);
    }
    check(actual==expected,"auto GER oracle");
  }
  puts("AUTONOMOUS_GER_PASS");
}
#endif
int main(int argc,char**argv) {
  Verilated::commandArgs(argc,argv);
  try{
    d.rst_ni=0;for(int i=0;i<4;i++)tick();d.rst_ni=1;tick();
#ifdef ARITH
    for(stall=0;stall<2;stall++){arithmetic();tile_tests();auto_ger(16);auto_ger(32);}
    fault_write=true;check(command(38,0x70000,0)==(uint64_t(1)<<63),"store fault");
    fault_write=false;check(command(38,0x70000,123)==0&&load(0x70000)==123,"store recovery");
    command(36,0x1000,0x20000);check(command(37,0)>>63,"invalid GER accepted");
    fault_at=reads+writes+2;command(36,0x1000,0x20000);
    check(command(37,16)>>63,"GER memory fault");fault_at=-1;auto_ger(16);
    puts("ALL_PASS");return 0;
#endif
#ifdef EXT
    bool packed=false; // The adopted EXT interface uses 16-byte slots.
    for(stall=0;stall<2;stall++){
      for(int l:{3,10})if(!(packed&&l==10)){
        for(auto shape:std::vector<std::vector<int>>{{1,1,1},{3,7,5},{4,16,4},{2,17,17},{1,255,1}})
          ext(shape[0],shape[1],shape[2],l,packed);
        ext(2,255,1,l,packed,true);
      }
    }
    stall=0;issue(1,0,0);check(wait()>>63,"invalid command accepted");
    // Fault then re-run with newly randomized operands catches partial cache reuse.
    fault_at=reads+writes+2;
    issue(0,0x1000,0x20000,0x60000);issue(1,3|(7<<8)|(5<<16),0);
    check(wait()>>63,"memory fault ignored");fault_at=-1;ext(3,7,5,3,false);
#endif
#ifdef MQ
    for(stall=0;stall<2;stall++)
      for(auto shape:std::vector<std::vector<int>>{{1,1},{7,17},{16,16},{17,19},{128,2}})
        mqtest(shape[0],shape[1]);
    stall=0;fault_at=reads+writes+2;
    issue(0,0x1000,0x20000,0x60000);issue(1,7|(17<<8),0);
    check(wait()>>63,"mq fault ignored");fault_at=-1;mqtest(7,17);
    issue(1,0,0);check(wait()>>63,"mq invalid accepted");
#endif
#ifdef LIN
    for(stall=0;stall<2;stall++)for(int l:{1,3,7,10,15,16})for(int count:{1,17,255})
      lintest(l,count);
    stall=0;fault_at=reads+writes+2;
    issue(0,0x1000,0x20000,0x60000);issue(1,7|(17<<8),0);
    check(wait()>>63,"lin fault ignored");fault_at=-1;lintest(7,17);
    issue(1,0,0);check(wait()>>63,"lin invalid accepted");
#endif
#ifdef Q
    for(stall=0;stall<2;stall++)for(int mode=0;mode<5;mode++)
      for(auto shape:std::vector<std::vector<int>>{{1,1,1},{3,17,5},{4,32,8},{2,17,17},{2,255,1}})
        qtest(shape[0],shape[1],shape[2],mode);
    stall=0;fault_at=reads+writes+2;
    issue(0,0x1000,0x20000,0x60000);issue(1,3|(17<<8)|(5<<16),0);
    check(wait()>>63,"q fault ignored");fault_at=-1;qtest(3,17,5,1);
    issue(1,0,0);check(wait()>>63,"q invalid accepted");
#endif
#ifdef SOLVE
    for(stall=0;stall<2;stall++)for(int bits:{4,8}){
      for(int n:{1,2,7,8,15,16,17,45,64})for(int kind:{0,1})solve(n,bits,kind);
      if(bits==8)for(int n:{72,96})for(int kind:{0,1})solve(n,bits,kind);
      solve(17,bits,2);
    }
    stall=0;fault_at=reads+writes+2;
    issue(0,0x1000,0x60000);issue(1,17,0);
    check(wait()>>63,"solve fault ignored");fault_at=-1;solve(17,4,1);
    issue(1,0,0);check(wait()>>63,"solve invalid accepted");
#endif
    if (std::getenv("XMPK_CACHE_EDGES")) {
#ifdef EXT
      for(stall=0;stall<2;stall++)for(int l:{3,10})if(!(packed&&l==10)){
        ext(2,16,16,l,packed,true); // Exact 256-entry tile, including address255.
        ext(2,129,2,l,packed);      // Smallest representable over-capacity tile.
      }
#endif
#ifdef Q
      for(stall=0;stall<2;stall++)for(int mode=0;mode<5;mode++){
        qtest(2,1,256,mode);
        qtest(2,1,257,mode);
      }
#endif
    }
    // Reset after active traffic, cancel the memory fixture's pending response,
    // then execute a fresh command to check stale-state recovery.
    issue(0,0x1000,0x20000,0x60000);
#ifdef SOLVE
    issue(1,1,0);
#elif defined(LIN) || defined(MQ)
    issue(1,1|(1<<8),0);
#else
    issue(1,1|(1<<8)|(1<<16),0);
#endif
    for(int i=0;i<3;i++)tick();
    d.rst_ni=0;pending=false;for(int i=0;i<4;i++)tick();
    pending=false;d.rst_ni=1;tick();stall=1;
#ifdef EXT
    ext(1,1,1,3,false);
#endif
#ifdef Q
    qtest(1,1,1,0);
#endif
#ifdef SOLVE
    solve(1,4,0);
#endif
#ifdef MQ
    mqtest(1,1);
#endif
#ifdef LIN
    lintest(1,1);
#endif
    puts("ALL_PASS");return 0;
  }catch(const std::exception&e){fprintf(stderr,"FAIL tick=%llu: %s\n",(unsigned long long)ticks,e.what());return 1;}
}
