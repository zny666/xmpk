/* QR-UOV L1 hotspot experiments, deliberately separate from production paths.
 * Flags: 1 Sign striped MxV; 2 constant-component dot; 4 Verify fixed products.
 * Timed region includes PRG, setup, packing, output materialization and erase.
 */
#include "../../source/r20/results/received/upstream/qruov/Reference_Implementation/ref/util.h"
#include "fql.h"
#include "qrop.h"
#include "util.h"
#include "keygen_trial.h"
#ifdef KEYGEN_NATIVE_CHECK
#include "qruov_common.h"
#include <assert.h>
#include <stdio.h>
#else
extern void printstr(const char *);
extern void printhex(uint64_t);
extern void __attribute__((noreturn)) tohost_exit(uintptr_t);
#endif
#ifndef TRIAL_FLAGS
#define TRIAL_FLAGS 0
#endif
#ifndef TRIAL_EQ_COUNT
#define TRIAL_EQ_COUNT 2
#endif
#ifndef TRIAL_EQ_BASE
#define TRIAL_EQ_BASE 0
#endif
#if QRUOV_q != 127 || QRUOV_L != 3 || QRUOV_fc != 1 || QRUOV_fc0 != 1 || QRUOV_fe != 1
#error These experiments require F127[x]/(x^3-x-1)
#endif

typedef struct { uint64_t setup, generate, pi1, other, cleanup, total; } metrics;
static qruov_l3_striped_vxv sdt_packed, pi2_packed, pi1;
static uint8_t sdt[QRUOV_M*QRUOV_V*3], p2[QRUOV_tau_n2];
static uint8_t p2t[QRUOV_tau_n2], p3[QRUOV_M*QRUOV_M*3];
static uint8_t cross[QRUOV_tau_n2], upper_product[P3_EQ_COEFF_LEN];
static uint8_t v[QRUOV_V*3], oil[QRUOV_M*3];
static uint8_t upper[TRIAL_EQ_COUNT*P3_EQ_COEFF_LEN];

static uint8_t dot0(const uint8_t *a, const uint8_t *b, int n)
{
    uint32_t sum = 0;
    for (int i=0; i<n; i++, a+=3, b+=3)
        sum += (uint32_t)a[0]*b[0] + (uint32_t)a[1]*b[2] +
               (uint32_t)a[2]*b[1];
    return gf_reduce(sum);
}

static uint8_t selected_dot(const uint8_t *a, const uint8_t *b, int n, unsigned flags)
{
    if (flags & 2) return dot0(a,b,n);
    uint8_t z[3];
    fql_dot(z,a,b,n);
    return z[0];
}

static void pack_rows(qruov_l3_striped_vxv dst, const uint8_t *src, int rows)
{
    for (int row=0; row<rows; row++)
        fql_pack_V_l3(dst[row], src+row*QRUOV_V*3);
}

/* Generate coefficients once, pack directly into transposed striped layout.
 * The sampler still needs its contiguous rejection-sampling buffer.
 */
static void generate_p2_striped(prg *rng, unsigned eq)
{
    prepare_pi2_coeffs_from_prg(p2,rng,eq);
    memset(pi2_packed,0,QRUOV_M*sizeof(qruov_l3_striped_v));
    for (int i=0; i<QRUOV_V; i++)
        for (int j=0; j<QRUOV_M; j++)
            for (int c=0; c<3; c++)
                pi2_packed[j][i/8][c] |=
                    (uint64_t)p2[(i*QRUOV_M+j)*3+c] << (8*(i%8));
    secure_zero(p2,sizeof(p2));
}

static void striped_mxv(uint8_t *dst, const qruov_l3_striped_vxv a,
                        const qruov_l3_striped_v b)
{
    for (int row=0; row<QRUOV_M; row++) {
        uint32_t sum[5]={0};
        for (int k=0; k<QRUOV_L3_STRIPED_CHUNKS; k++)
            for (int i=0; i<3; i++)
                for (int j=0; j<3; j++)
                    sum[i+j] += fql_l3_sum_bytes(xmpk_gf127_mul8(a[row][k][i],b[k][j]));
        sum[1]+=sum[4]; sum[2]+=sum[4];
        sum[0]+=sum[3]; sum[1]+=sum[3];
        for (int c=0; c<3; c++) dst[row*3+c]=gf_reduce(sum[c]);
    }
}

static void sign_trial(uint8_t *out, const SEED_PK seed, unsigned flags, metrics *m)
{
    memset(m,0,sizeof(*m));
    uint64_t total=keygen_trial_cycle(), t=total;
    prg rng;
    qruov_l3_striped_v pv,presult;
    uint8_t a[QRUOV_V*3], b[QRUOV_M*3], c[QRUOV_M*3], d[QRUOV_M*3];
    prg_init(&rng,seed);
    fql_pack_V_l3(pv,v);
    int prepared=0;
    if (flags&1) pack_rows(sdt_packed,sdt,QRUOV_M);
    else prepared=fql_prepare_MxV(sdt);
    m->setup=keygen_trial_cycle()-t;
    for (unsigned item=0; item<TRIAL_EQ_COUNT; item++) {
        t=keygen_trial_cycle();
        prepare_pi1_striped_from_prg(pi1,&rng,TRIAL_EQ_BASE+item);
        if (flags&1) generate_p2_striped(&rng,TRIAL_EQ_BASE+item);
        else prepare_pi2t_from_prg(p2t,&rng,TRIAL_EQ_BASE+item);
        m->generate+=keygen_trial_cycle()-t;
        t=keygen_trial_cycle();
        fql_mul_striped_VxV_V_l3(a,pi1,pv);
        out[item*(QRUOV_m+1)+QRUOV_m]=selected_dot(v,a,QRUOV_V,flags);
        m->pi1+=keygen_trial_cycle()-t;
        t=keygen_trial_cycle();
        if (flags&1) {
            fql_pack_V_l3(presult,a);
            striped_mxv(b,sdt_packed,presult);
            striped_mxv(c,pi2_packed,pv);
        } else {
            fql_mul_prepared_MxV_V(b,sdt,a,prepared);
            fql_mul_MxV_V(c,p2t,v);
        }
        fql_sub_M(d,c,b); fql_add_M(d,d,d);
        fql_perm_M(out+item*(QRUOV_m+1),d);
        m->other+=keygen_trial_cycle()-t;
    }
    t=keygen_trial_cycle();
    prg_free(&rng);
    secure_zero(a,sizeof(a)); secure_zero(b,sizeof(b));
    secure_zero(c,sizeof(c)); secure_zero(d,sizeof(d));
    secure_zero(pv,sizeof(pv));
    if (flags&1) {
        secure_zero(presult,sizeof(presult));
        secure_zero(sdt_packed,QRUOV_M*sizeof(qruov_l3_striped_v));
    } else xmpk_gf127_extgemm_scrub_workspace();
    m->cleanup=keygen_trial_cycle()-t; m->total=keygen_trial_cycle()-total;
}

static void prepare_fixed_products(void)
{
    for (int i=0; i<QRUOV_V; i++)
        for (int j=0; j<QRUOV_M; j++)
            fql_mul(cross+(i*QRUOV_M+j)*3,v+i*3,oil+j*3);
    unsigned offset=0;
    for (int i=0; i<QRUOV_M; i++)
        for (int j=i; j<QRUOV_M; j++,offset+=3) {
            fql_mul(upper_product+offset,oil+i*3,oil+j*3);
            if (i!=j) fql_add(upper_product+offset,upper_product+offset,upper_product+offset);
        }
}

static void verify_trial(uint8_t *out, const SEED_PK seed, unsigned flags, metrics *m)
{
    memset(m,0,sizeof(*m));
    uint64_t total=keygen_trial_cycle(),t=total;
    prg rng;
    qruov_l3_striped_v pv;
    uint8_t a[QRUOV_V*3],b[QRUOV_V*3],c[QRUOV_M*3];
    prg_init(&rng,seed); fql_pack_V_l3(pv,v);
    if (flags&4) prepare_fixed_products();
    m->setup=keygen_trial_cycle()-t;
    for (unsigned item=0; item<TRIAL_EQ_COUNT; item++) {
        t=keygen_trial_cycle();
        prepare_pi1_striped_from_prg(pi1,&rng,TRIAL_EQ_BASE+item);
        prepare_pi2_coeffs_from_prg(p2,&rng,TRIAL_EQ_BASE+item);
        m->generate+=keygen_trial_cycle()-t;
        t=keygen_trial_cycle();
        fql_mul_striped_VxV_V_l3(a,pi1,pv);
        uint8_t z0=selected_dot(v,a,QRUOV_V,flags);
        m->pi1+=keygen_trial_cycle()-t;
        t=keygen_trial_cycle();
        uint8_t z1,z2;
        if (flags&4) {
            z1=dot0(p2,cross,QRUOV_V*QRUOV_M);
            z2=dot0(upper+item*P3_EQ_COEFF_LEN,upper_product,P3_EQ_COEFF_LEN/3);
        } else {
            fql_mul_VxM_M(b,p2,oil);
            z1=selected_dot(v,b,QRUOV_V,flags);
            unpack_upper_to_symm_MxM(p3,upper+item*P3_EQ_COEFF_LEN);
            fql_mul_MxM_M(c,p3,oil);
            z2=selected_dot(oil,c,QRUOV_M,flags);
        }
        out[item*4]=z0; out[item*4+1]=z1; out[item*4+2]=z2;
        out[item*4+3]=gf_add(gf_add(z0,gf_add(z1,z1)),z2);
        m->other+=keygen_trial_cycle()-t;
    }
    t=keygen_trial_cycle();
    prg_free(&rng); secure_zero(pv,sizeof(pv));
    secure_zero(a,sizeof(a));
    if (flags&4) {
        secure_zero(cross,sizeof(cross)); secure_zero(upper_product,sizeof(upper_product));
    } else {
        secure_zero(b,sizeof(b)); secure_zero(c,sizeof(c));
        xmpk_gf127_extgemm_scrub_workspace();
    }
    m->cleanup=keygen_trial_cycle()-t; m->total=keygen_trial_cycle()-total;
}

#define OUTPUT_BYTES (TRIAL_EQ_COUNT*(QRUOV_m+1+4))
static uint8_t output[OUTPUT_BYTES];
static void initialize(SEED_PK seed,unsigned trial)
{
    for (unsigned i=0; i<sizeof(SEED_PK); i++) seed[i]=17*i+3+trial*43;
    for (unsigned i=0; i<sizeof(v); i++) v[i]=(i*31+trial*23)%127;
    for (unsigned i=0; i<sizeof(oil); i++) oil[i]=(i*67+trial*53)%127;
    for (unsigned i=0; i<sizeof(sdt); i++) sdt[i]=(i*41+trial*73)%127;
    for (unsigned i=0; i<sizeof(upper); i++) upper[i]=(i*79+trial*13)%127;
    if (trial==3) { memset(v,126,sizeof(v)); memset(oil,126,sizeof(oil)); }
    if (trial==4) { memset(v,0,sizeof(v)); memset(oil,0,sizeof(oil)); }
}

#if defined(KEYGEN_NATIVE_CHECK) && TRIAL_EQ_COUNT == QRUOV_m && TRIAL_EQ_BASE == 0
/* One complete signature closure, functional only. The RTL experiment stays
 * the small hotspot test; no full simulated signing workload is launched. */
static void native_signature_closure(void)
{
    SEED_PK seed;
    SEED_SK sk,sy,sr,ss;
    uint8_t sd[QRUOV_tau_n2],pk[P3_SERIALIZED_LEN];
    uint8_t eqn[QRUOV_m*QRUOV_m],u[QRUOV_m],target[QRUOV_m];
    SIG_S expected,actual;
    SALT expected_r,actual_r;
    const uint8_t msg[]="Xmpk Sign/Verify native closure";
    metrics m;
    initialize(seed,0);
    for (unsigned i=0; i<sizeof(sk); i++) {
        sk[i]=29*i+5; sy[i]=31*i+7; sr[i]=37*i+11; ss[i]=41*i+13;
    }
    qruov_keygen(seed,sk,pk);
    assert(qruov_sign(seed,sk,sy,sr,ss,msg,sizeof(msg)-1,expected_r,expected)==0);
    assert(qruov_verify(seed,pk,msg,sizeof(msg)-1,expected_r,expected)==1);
    prepare_sd_from_seed(sd,sk); prepare_sdt_from_sd(sdt,sd);
    prepare_y_from_seed(v,sy);
    sign_trial(output,seed,7,&m);
    for (unsigned eq=0; eq<QRUOV_m; eq++) {
        memcpy(eqn+eq*QRUOV_m,output+eq*(QRUOV_m+1),QRUOV_m);
        u[eq]=output[eq*(QRUOV_m+1)+QRUOV_m];
    }
    assert(qruov_common_sign_solve_oil(oil,actual_r,eqn,u,seed,sr,ss,msg,sizeof(msg)-1)==0);
    qruov_common_sig_s(actual,sd,v,oil);
    assert(memcmp(expected_r,actual_r,sizeof(actual_r))==0);
    assert(memcmp(expected,actual,sizeof(actual))==0);
    memcpy(v,actual,sizeof(v)); memcpy(oil,actual+sizeof(v),sizeof(oil));
    size_t offset=0;
    for (unsigned eq=0; eq<QRUOV_m; eq+=P3_CHUNK_EQS) {
        unsigned n=QRUOV_m-eq;
        if (n>P3_CHUNK_EQS) n=P3_CHUNK_EQS;
        assert(load_fq_checked(upper+eq*P3_EQ_COEFF_LEN,pk+offset,n*P3_EQ_COEFF_LEN));
        offset+=BITS2BYTE(n*P3_EQ_COEFF_LEN*QRUOV_q_LOG);
    }
    verify_trial(output,seed,7,&m);
    compute_mu_and_hash(target,seed,msg,sizeof(msg)-1,actual_r);
    for (unsigned eq=0; eq<QRUOV_m; eq++) assert(output[eq*4+3]==target[eq]);
    /* Reject a perturbed signature as well. */
    v[0]=gf_add(v[0],1);
    verify_trial(output,seed,7,&m);
    unsigned mismatches=0;
    for (unsigned eq=0; eq<QRUOV_m; eq++) mismatches+=output[eq*4+3]!=target[eq];
    assert(mismatches);
    puts("NATIVE_FUNCTIONAL_ONLY: exact signature + valid/tampered verification PASS");
}
#endif

#ifndef KEYGEN_NATIVE_CHECK
static void metric(const char *name,uint64_t value)
{ printstr(name); printstr("=0x"); printhex(value); printstr("\n"); }
static void emit(const metrics *m,int sign)
{
    metric(sign?"sign_setup":"verify_setup",m->setup);
    metric(sign?"sign_generate":"verify_generate",m->generate);
    metric(sign?"sign_pi1":"verify_pi1",m->pi1);
    metric(sign?"sign_other":"verify_other",m->other);
    metric(sign?"sign_cleanup":"verify_cleanup",m->cleanup);
    metric(sign?"sign_total":"verify_total",m->total);
}
#endif
#ifdef KEYGEN_NATIVE_CHECK
int main(int argc, char **argv)
#else
int main(void)
#endif
{
    SEED_PK seed;
    metrics s,w;
#ifdef KEYGEN_NATIVE_CHECK
    uint8_t reference[OUTPUT_BYTES];
    unsigned cases=0;
    for (unsigned trial=0; trial<5; trial++) {
        initialize(seed,trial);
        sign_trial(reference,seed,0,&s);
        verify_trial(reference+TRIAL_EQ_COUNT*(QRUOV_m+1),seed,0,&w);
        if (trial==0 && argc==2) {
            FILE *golden=fopen(argv[1],"wb");
            assert(golden);
            assert(fwrite(reference,1,sizeof(reference),golden)==sizeof(reference));
            assert(fclose(golden)==0);
        }
        for (unsigned flags=1; flags<8; flags++) {
            sign_trial(output,seed,flags,&s);
            verify_trial(output+TRIAL_EQ_COUNT*(QRUOV_m+1),seed,flags,&w);
            if (memcmp(reference,output,sizeof(output))) {
                for (unsigned i=0; i<sizeof(output); i++)
                    if (reference[i]!=output[i])
                        fprintf(stderr,"trial=%u flags=%u index=%u expected=%u actual=%u\n",
                                trial,flags,i,reference[i],output[i]);
                assert(0);
            }
            cases++;
        }
    }
    printf("NATIVE_FUNCTIONAL_ONLY: %u Sign/Verify comparisons PASS\n",cases);
#if TRIAL_EQ_COUNT == QRUOV_m && TRIAL_EQ_BASE == 0
    native_signature_closure();
#endif
#else
    initialize(seed,0);
    sign_trial(output,seed,TRIAL_FLAGS,&s);
    verify_trial(output+TRIAL_EQ_COUNT*(QRUOV_m+1),seed,TRIAL_FLAGS,&w);
    metric("flags",TRIAL_FLAGS); metric("eq_base",TRIAL_EQ_BASE);
    metric("eq_count",TRIAL_EQ_COUNT);
    emit(&s,1); emit(&w,0);
    static char hex[OUTPUT_BYTES*2+2];
    const char digits[]="0123456789abcdef";
    for (unsigned i=0; i<sizeof(output); i++) {
        hex[2*i]=digits[output[i]>>4]; hex[2*i+1]=digits[output[i]&15];
    }
    hex[OUTPUT_BYTES*2]='\n'; hex[OUTPUT_BYTES*2+1]=0;
    printstr("SERIALIZED_HEX="); printstr(hex);
    printstr("SIGNVERIFY_SHORT_SUCCESS\n"); tohost_exit(0);
#endif
    return 0;
}
