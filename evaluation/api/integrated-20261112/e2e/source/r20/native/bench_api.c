#define _GNU_SOURCE
#include "ref_api.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void fail(const char *message){perror(message);exit(2);}
static void wipe(void *p,size_t n){volatile uint8_t *v=p;while(n--)*v++=0;}
static uint64_t ns(clockid_t clock){struct timespec t;if(clock_gettime(clock,&t))fail("clock");return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;}
static void *alloc(size_t count,size_t size){if(!count||!size||count>SIZE_MAX/size)fail("allocation size");void *p=calloc(count,size);if(!p)fail("calloc");return p;}
static void transfer(int fd,void *p,size_t n,int writing){uint8_t *s=p;while(n){ssize_t r=writing?write(fd,s,n):read(fd,s,n);if(r<0&&errno==EINTR)continue;if(r<=0)fail("fixture IO");n-=r;s+=r;}}
static void fixture(const char *path,uint8_t *pk,uint8_t *sk,int creating){
    const uint8_t magic[8]={'R','2','0','K','E','Y',1,0};uint8_t got[8];size_t pn=ref_public_bytes(),sn=ref_secret_bytes();
    int fd=open(path,creating?(O_WRONLY|O_CREAT|O_EXCL):O_RDONLY,0600);if(fd<0)fail("fixture");
    if(creating){if(ref_keypair(pk,sk))fail("fixture keypair");transfer(fd,(void*)magic,8,1);transfer(fd,pk,pn,1);transfer(fd,sk,sn,1);}
    else{transfer(fd,got,8,0);if(memcmp(got,magic,8))fail("fixture magic");transfer(fd,pk,pn,0);transfer(fd,sk,sn,0);uint8_t extra;if(read(fd,&extra,1)!=0)fail("fixture trailing");}
    close(fd);
}
static void gate(const char *cmd){
    const char *c=getenv("BENCH_CONTROL_FD"),*a=getenv("BENCH_ACK_FD");if(!c&&!a)return;if(!c||!a)fail("incomplete perf control");
    int ctl=atoi(c),ack=atoi(a);transfer(ctl,(void*)cmd,strlen(cmd),1);
    char buffer[sizeof("ack\n")];size_t used=0;
    while(used<sizeof(buffer)){
        struct pollfd p={ack,POLLIN,0};int r=poll(&p,1,10000);if(r<0&&errno==EINTR)continue;if(r<=0)fail("perf ACK timeout");
        ssize_t n=read(ack,buffer+used,sizeof(buffer)-used);if(n<0&&errno==EINTR)continue;if(n<=0)fail("perf ACK read");used+=n;
    }
    if(memcmp(buffer,"ack\n",sizeof(buffer)))fail("unexpected perf ACK");
}
static int verify_one(uint8_t *work,const uint8_t *sm,size_t slen,const uint8_t *message,size_t mlen,const uint8_t *pk){
    size_t n=0;return ref_open(work,&n,sm,slen,pk)==0&&n==mlen&&!memcmp(work,message,mlen);
}
int main(int argc,char **argv){
    struct rlimit core={0,0};setrlimit(RLIMIT_CORE,&core);
    size_t pn=ref_public_bytes(),sn=ref_secret_bytes(),sig=ref_signature_bytes();
    if(argc==2&&!strcmp(argv[1],"--metadata")){
        printf("{\"algorithm\":\"%s\",\"parameter\":\"%s\",\"implementation\":\"%s\",\"public_bytes\":%zu,\"secret_bytes\":%zu,\"signature_max_bytes\":%zu,\"message_bytes\":32,\"api\":\"signed-message\"}\n",ref_algorithm(),ref_parameter(),ref_implementation(),pn,sn,sig);return 0;}
    if(argc<2)fail("arguments");uint8_t message[32];for(size_t i=0;i<32;i++)message[i]=(uint8_t)i;
    uint8_t *pk=alloc(1,pn),*sk=alloc(1,sn),*work=alloc(1,sig+32),*sample=alloc(1,sig+32);size_t sample_len=0;
    if(!strcmp(argv[1],"--fixture")){
        if(argc!=3)fail("fixture args");fixture(argv[2],pk,sk,1);wipe(sk,sn);free(sk);free(pk);free(work);free(sample);puts("{\"status\":\"fixture_created_private_not_for_archive\"}");return 0;}
    if(!strcmp(argv[1],"--sanity")){
        if(ref_keypair(pk,sk)||ref_sign(sample,&sample_len,message,32,sk)||sample_len>sig+32||!verify_one(work,sample,sample_len,message,32,pk))fail("positive signature test");
        sample[0]^=1;size_t n=0;int rejected=ref_open(work,&n,sample,sample_len,pk)!=0;sample[0]^=1;
        if(!rejected)fail("modified signature accepted");sample[sample_len-1]^=1;n=0;rejected=ref_open(work,&n,sample,sample_len,pk)!=0;sample[sample_len-1]^=1;
        if(!rejected)fail("modified signed message accepted");n=0;if(ref_open(work,&n,sample,sample_len-1,pk)==0)fail("truncated signature accepted");
        wipe(sk,sn);free(sk);free(pk);free(work);free(sample);puts("{\"status\":\"PASS\",\"positive\":true,\"changed_signature_rejected\":true,\"changed_message_rejected\":true,\"truncation_rejected\":true}");return 0;}
    if(argc!=4)fail("operation fixture count");const char *op=argv[1];char *end;unsigned long count=strtoul(argv[3],&end,10);if(*end||!count||count>100000)fail("count");
    int kind=!strcmp(op,"keygen")?0:!strcmp(op,"sign")?1:!strcmp(op,"verify")?2:-1;if(kind<0)fail("operation");
    fixture(argv[2],pk,sk,0);
    if(kind==2&&(ref_sign(sample,&sample_len,message,32,sk)||!verify_one(work,sample,sample_len,message,32,pk)))fail("verification fixture");
    size_t stride=sig+32;uint8_t *outputs=NULL,*generated_pk=NULL,*generated_sk=NULL;size_t *lengths=NULL;
    if(kind==1){if(count>134217728ULL/stride)fail("signature retention cap");outputs=alloc(count,stride);lengths=alloc(count,sizeof(size_t));}
    if(kind==0){if(count>134217728ULL/(pn+sn))fail("key retention cap");generated_pk=alloc(count,pn);generated_sk=alloc(count,sn);}
    struct rusage before,after;if(getrusage(RUSAGE_THREAD,&before))fail("rusage");
    gate("enable\n");alarm(30);int cpu_start=sched_getcpu();uint64_t cpu0=ns(CLOCK_THREAD_CPUTIME_ID),wall0=ns(CLOCK_MONOTONIC_RAW);int error=0;unsigned long done=0;
    for(;done<count;done++){
        if(kind==0)error=ref_keypair(generated_pk+done*pn,generated_sk+done*sn);
        else if(kind==1)error=ref_sign(outputs+done*stride,&lengths[done],message,32,sk);
        else {size_t n=0;error=ref_open(work,&n,sample,sample_len,pk);if(!error&&(n!=32||memcmp(work,message,32)))error=-1;}
        if(error)break;
    }
    uint64_t wall1=ns(CLOCK_MONOTONIC_RAW),cpu1=ns(CLOCK_THREAD_CPUTIME_ID);int cpu_end=sched_getcpu();alarm(0);gate("disable\n");if(getrusage(RUSAGE_THREAD,&after))fail("rusage");
    if(error||done!=count)fail("measured API failure");size_t verified=0;
    if(kind==1){for(unsigned long i=0;i<count;i++){if(lengths[i]>stride||!verify_one(work,outputs+i*stride,lengths[i],message,32,pk))fail("recorded signature verification");verified++;}}
    if(kind==0){for(unsigned long i=0;i<count;i++){size_t n=0;if(ref_sign(sample,&n,message,32,generated_sk+i*sn)||n>stride||!verify_one(work,sample,n,message,32,generated_pk+i*pn))fail("generated key verification");verified++;}}
    printf("{\"status\":\"ok\",\"algorithm\":\"%s\",\"parameter\":\"%s\",\"operation\":\"%s\",\"iterations\":%lu,\"wall_ns\":%"PRIu64",\"thread_cpu_ns\":%"PRIu64",\"cpu_start\":%d,\"cpu_end\":%d,\"voluntary_switches\":%ld,\"involuntary_switches\":%ld,\"minor_faults\":%ld,\"major_faults\":%ld,\"postcheck_outputs\":%zu,\"verification_fixture_valid\":true,\"perf_gated\":%s,\"includes\":\"original signed-message API, internal RNG and expansion, loop overhead; external fixture and postverification outside gate\"}\n",ref_algorithm(),ref_parameter(),op,count,wall1-wall0,cpu1-cpu0,cpu_start,cpu_end,after.ru_nvcsw-before.ru_nvcsw,after.ru_nivcsw-before.ru_nivcsw,after.ru_minflt-before.ru_minflt,after.ru_majflt-before.ru_majflt,verified,getenv("BENCH_CONTROL_FD")?"true":"false");
    if(generated_sk){wipe(generated_sk,count*sn);free(generated_sk);free(generated_pk);}if(outputs){wipe(outputs,count*stride);free(outputs);free(lengths);}wipe(sk,sn);wipe(sample,stride);wipe(work,stride);free(sk);free(pk);free(sample);free(work);return 0;
}
