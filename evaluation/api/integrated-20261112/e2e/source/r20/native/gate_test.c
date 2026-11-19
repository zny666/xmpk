#define _GNU_SOURCE
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
static uint64_t now(clockid_t id){struct timespec t;if(clock_gettime(id,&t))abort();return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void control(const char *command){int c=atoi(getenv("BENCH_CONTROL_FD")),a=atoi(getenv("BENCH_ACK_FD"));if(write(c,command,strlen(command))!=(ssize_t)strlen(command))abort();char ack[sizeof("ack\n")];size_t n=0;while(n<sizeof(ack)){struct pollfd p={a,POLLIN,0};if(poll(&p,1,5000)<=0)abort();ssize_t r=read(a,ack+n,sizeof(ack)-n);if(r<=0)abort();n+=r;}if(memcmp(ack,"ack\n",sizeof(ack)))abort();}
__attribute__((noinline)) static uint64_t outside_gate_marker(uint64_t n){volatile uint64_t s=1;for(uint64_t i=0;i<n;i++)s=(s^i)*6364136223846793005ULL+1;return s;}
__attribute__((noinline)) static uint64_t inside_gate_marker(uint64_t n){volatile uint64_t s=7;for(uint64_t i=0;i<n;i++)s=(s^i)*6364136223846793005ULL+1;return s;}
int main(int argc,char**argv){(void)argc;uint64_t sink=0;uint64_t end=now(CLOCK_MONOTONIC_RAW)+300000000;while(now(CLOCK_MONOTONIC_RAW)<end)sink^=outside_gate_marker(100000);control("enable\n");int cpu=sched_getcpu();uint64_t c0=now(CLOCK_THREAD_CPUTIME_ID),w0=now(CLOCK_MONOTONIC_RAW);end=w0+600000000;while(now(CLOCK_MONOTONIC_RAW)<end)sink^=inside_gate_marker(100000);uint64_t w=now(CLOCK_MONOTONIC_RAW)-w0,c=now(CLOCK_THREAD_CPUTIME_ID)-c0;control("disable\n");end=now(CLOCK_MONOTONIC_RAW)+300000000;while(now(CLOCK_MONOTONIC_RAW)<end)sink^=outside_gate_marker(100000);printf("{\"status\":\"ok\",\"iterations\":%lu,\"wall_ns\":%llu,\"thread_cpu_ns\":%llu,\"cpu_start\":%d,\"cpu_end\":%d,\"perf_gated\":true,\"test_sink\":%llu}\n",strtoul(argv[3],0,10),(unsigned long long)w,(unsigned long long)c,cpu,sched_getcpu(),(unsigned long long)sink);return 0;}
