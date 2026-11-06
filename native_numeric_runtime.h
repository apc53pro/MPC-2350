#ifndef NATIVE_NUMERIC_RUNTIME_H
#define NATIVE_NUMERIC_RUNTIME_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "narm_numeric_ops.h"

#if defined(__arm__) && defined(PICO_RP2350)
#include <hardware/exception.h>
#endif

namespace narm_numeric {
#if defined(__arm__) && defined(PICO_RP2350)
static inline uint32_t suspend_preemption() {
    uint32_t previous;
    uint32_t threshold=PICO_LOWEST_EXCEPTION_PRIORITY;
    // PendSV is configured at the lowest priority by the native scheduler.
    // Higher-priority keyboard/USB/timer interrupts can still run; their DCP
    // use is LIFO-nested and protected by the SDK's engaged-state mechanism.
    __asm__ __volatile__("mrs %0, basepri\nmsr basepri_max, %1\ndsb\nisb"
                         : "=&r"(previous) : "r"(threshold) : "memory");
    return previous;
}
static inline void resume_preemption(uint32_t previous) {
    __asm__ __volatile__("msr basepri, %0\nisb" :: "r"(previous) : "memory");
}
#endif
static inline double as_double(uint64_t bits){double value;memcpy(&value,&bits,8);return value;}
static inline uint64_t as_bits(double value){uint64_t bits;memcpy(&bits,&value,8);return bits;}
static inline uint64_t round_shift(uint64_t value,unsigned shift) {
    if(!shift)return value;
    if(shift>64)return 0;
    if(shift==64)return value>UINT64_C(0x8000000000000000)?1u:0u;
    uint64_t result=value>>shift,tail=value&((UINT64_C(1)<<shift)-1u),half=UINT64_C(1)<<(shift-1u);
    return result+(tail>half||(tail==half&&(result&1u)));
}
static inline unsigned top_bit(uint64_t x) {
    return (x>>32)?63u-(unsigned)__builtin_clz((uint32_t)(x>>32)):
                     31u-(unsigned)__builtin_clz((uint32_t)x);
}
static inline uint64_t shift_jam(uint64_t value,unsigned amount) {
    if(!amount)return value;
    if(amount>=64u)return value!=0;
    return (value>>amount)|((value<<(64u-amount))!=0);
}
struct Finite {uint64_t mantissa;int exponent;};
static inline Finite unpack_finite(uint64_t bits) {
    unsigned raw=(unsigned)(bits>>52)&2047u;
    uint64_t mantissa=bits&UINT64_C(0xfffffffffffff);
    int exponent=int(raw)-1023;
    if(raw)mantissa|=UINT64_C(1)<<52;
    else {
        unsigned shift=52u-top_bit(mantissa);
        mantissa<<=shift;exponent=-1022-int(shift);
    }
    return {mantissa,exponent};
}
static inline uint64_t pack_rounded(bool negative,int exponent,uint64_t extended) {
    uint64_t sign=uint64_t(negative)<<63;
    if(!extended)return sign;
    unsigned top=top_bit(extended);
    if(top>55u){extended=shift_jam(extended,top-55u);exponent+=int(top-55u);}
    else if(top<55u){extended<<=55u-top;exponent-=int(55u-top);}
    if(exponent < -1022){extended=shift_jam(extended,unsigned(-1022-exponent));exponent=-1022;}
    uint64_t mantissa=extended>>3;unsigned tail=(unsigned)extended&7u;
    if(tail>4u||(tail==4u&&(mantissa&1u)))++mantissa;
    if(mantissa>=(UINT64_C(1)<<53)){mantissa>>=1;++exponent;}
    if(exponent>1023)return sign|UINT64_C(0x7ff0000000000000);
    unsigned raw=exponent==-1022&&mantissa<(UINT64_C(1)<<52)?0u:unsigned(exponent+1023);
    return sign|(uint64_t(raw)<<52)|(mantissa&UINT64_C(0xfffffffffffff));
}
static inline uint64_t quiet_nan(uint64_t a,uint64_t b) {
    const uint64_t infinity=UINT64_C(0x7ff0000000000000),magnitude=UINT64_C(0x7fffffffffffffff);
    if((a&magnitude)>infinity)return a|UINT64_C(0x8000000000000);
    if((b&magnitude)>infinity)return b|UINT64_C(0x8000000000000);
    return UINT64_C(0x7ff8000000000000);
}
// These rare-case helpers are ordinary firmware text. Avoid references to the
// toolchain's real __aeabi_df* helpers: the Arduino linker puts libgcc in SRAM.
static inline uint64_t add_strict(uint64_t a,uint64_t b) {
    const uint64_t infinity=UINT64_C(0x7ff0000000000000),magnitude=UINT64_C(0x7fffffffffffffff);
    uint64_t aa=a&magnitude,bb=b&magnitude;
    if(aa>infinity||bb>infinity)return quiet_nan(a,b);
    if(aa==infinity||bb==infinity) {
        if(aa==infinity&&bb==infinity&&((a^b)>>63))return quiet_nan(0,0);
        return aa==infinity?a:b;
    }
    if(!aa)return !bb?(a&b&UINT64_C(0x8000000000000000)):b;
    if(!bb)return a;
    Finite x=unpack_finite(a),y=unpack_finite(b);
    if(x.exponent<y.exponent||(x.exponent==y.exponent&&x.mantissa<y.mantissa)) {
        Finite t=x;x=y;y=t;uint64_t bits=a;a=b;b=bits;
    }
    uint64_t left=x.mantissa<<3,right=shift_jam(y.mantissa<<3,unsigned(x.exponent-y.exponent));
    bool same=((a^b)>>63)==0;uint64_t result=same?left+right:left-right;
    return pack_rounded(result?(a>>63)!=0:false,x.exponent,result);
}
static inline uint64_t multiply_strict(uint64_t a,uint64_t b) {
    const uint64_t infinity=UINT64_C(0x7ff0000000000000),magnitude=UINT64_C(0x7fffffffffffffff);
    uint64_t aa=a&magnitude,bb=b&magnitude,sign=(a^b)&~magnitude;
    if(aa>infinity||bb>infinity)return quiet_nan(a,b);
    if(aa==infinity||bb==infinity)return (!aa||!bb)?quiet_nan(0,0):sign|infinity;
    if(!aa||!bb)return sign;
    Finite x=unpack_finite(a),y=unpack_finite(b);
    uint32_t a0=(uint32_t)x.mantissa,a1=(uint32_t)(x.mantissa>>32),
             b0=(uint32_t)y.mantissa,b1=(uint32_t)(y.mantissa>>32);
    uint64_t lo=uint64_t(a0)*b0,cross1=uint64_t(a0)*b1,cross2=uint64_t(a1)*b0;
    uint64_t old=lo;lo+=cross1<<32;uint64_t carry=lo<old;
    old=lo;lo+=cross2<<32;carry+=lo<old;
    uint64_t hi=uint64_t(a1)*b1+(cross1>>32)+(cross2>>32)+carry;
    unsigned shift=(hi&(UINT64_C(1)<<41))?50u:49u;
    uint64_t extended=(lo>>shift)|(hi<<(64u-shift));
    if(lo&((UINT64_C(1)<<shift)-1u))extended|=1u;
    return pack_rounded(sign!=0,x.exponent+y.exponent+(shift==50u),extended);
}
static inline uint64_t divide_strict(uint64_t a,uint64_t b) {
    const uint64_t infinity=UINT64_C(0x7ff0000000000000),magnitude=UINT64_C(0x7fffffffffffffff);
    uint64_t aa=a&magnitude,bb=b&magnitude,sign=(a^b)&~magnitude;
    if(aa>infinity||bb>infinity)return quiet_nan(a,b);
    if(aa==infinity)return bb==infinity?quiet_nan(0,0):sign|infinity;
    if(bb==infinity)return sign;
    if(!bb)return !aa?quiet_nan(0,0):sign|infinity;
    if(!aa)return sign;
    Finite x=unpack_finite(a),y=unpack_finite(b);
    uint64_t remainder=x.mantissa,quotient=0;int exponent=x.exponent-y.exponent;
    if(remainder<y.mantissa){remainder<<=1;--exponent;}
    for(unsigned digit=0;digit<56u;++digit) {
        quotient<<=1;if(remainder>=y.mantissa){remainder-=y.mantissa;quotient|=1u;}
        remainder<<=1;
    }
    if(remainder)quotient|=1u;
    return pack_rounded(sign!=0,exponent,quotient);
}
static inline uint64_t integer_float(uint64_t value,bool signed_input,bool single) {
    bool negative=signed_input&&(value>>63);uint64_t magnitude=negative?0-value:value;
    if(!magnitude)return 0;
    unsigned top=top_bit(magnitude),precision=single?23u:52u,bias=single?127u:1023u;
    uint64_t significand=top>precision?round_shift(magnitude,top-precision):magnitude<<(precision-top);
    if(significand==(UINT64_C(1)<<(precision+1u))){significand>>=1;++top;}
    return (uint64_t(negative)<<(single?31u:63u))|((uint64_t(top+bias))<<precision)|
           (significand&((UINT64_C(1)<<precision)-1u));
}
static inline uint64_t float_integer(uint64_t bits,bool single,unsigned width,bool unsigned_output) {
    unsigned fraction=single?23u:52u,ebits=single?8u:11u,bias=single?127u:1023u;
    bool negative=(bits>>(fraction+ebits))!=0;
    unsigned exponent=(unsigned)((bits>>fraction)&((1u<<ebits)-1u));
    uint64_t mantissa=bits&((UINT64_C(1)<<fraction)-1u);
    if(exponent==((1u<<ebits)-1u)&&mantissa)return 0; // deterministic NaN conversion
    if(unsigned_output&&negative)return 0;
    int power=int(exponent)-int(bias);
    uint64_t limit=unsigned_output?(width==64?UINT64_MAX:UINT64_C(0xffffffff)):
        negative?(UINT64_C(1)<<(width-1u)):(UINT64_C(1)<<(width-1u))-1u;
    if(power<0)return 0;
    if(power>=int(width-(unsigned_output?0u:1u)))return negative?0-limit:limit;
    mantissa|=UINT64_C(1)<<fraction;
    uint64_t magnitude=power>=int(fraction)?mantissa<<(unsigned(power)-fraction):mantissa>>(fraction-unsigned(power));
    if(magnitude>limit)magnitude=limit;
    return negative?0-magnitude:magnitude;
}
static inline uint64_t widen_float(uint32_t bits) {
    uint64_t sign=uint64_t(bits&0x80000000u)<<32;
    unsigned exponent=(bits>>23)&255u;uint32_t mantissa=bits&0x7fffffu;
    if(exponent==255u)return sign|UINT64_C(0x7ff0000000000000)|(uint64_t(mantissa)<<29)|
        (mantissa?UINT64_C(0x8000000000000):0);
    if(!exponent) {
        if(!mantissa)return sign;
        unsigned top=31u-(unsigned)__builtin_clz(mantissa);
        return sign|(uint64_t(int(top)-149+1023)<<52)|((uint64_t(mantissa)<<(52u-top))&UINT64_C(0xfffffffffffff));
    }
    return sign|(uint64_t(exponent-127u+1023u)<<52)|(uint64_t(mantissa)<<29);
}
static inline uint32_t narrow_double(uint64_t bits) {
    uint32_t sign=(uint32_t)(bits>>32)&0x80000000u;
    unsigned exponent=(unsigned)(bits>>52)&2047u;uint64_t mantissa=bits&UINT64_C(0xfffffffffffff);
    if(exponent==2047u)return sign|0x7f800000u|(mantissa?((uint32_t)(mantissa>>29)|0x400000u):0u);
    if(!exponent&&!mantissa)return sign;
    int power=int(exponent)-1023;
    if(exponent)mantissa|=UINT64_C(1)<<52;
    else {unsigned top=top_bit(mantissa);mantissa<<=52u-top;power=-1022-int(52u-top);}
    unsigned shift=29u+(power < -126?unsigned(-126-power):0u);
    uint64_t significand=round_shift(mantissa,shift);
    if(power < -126)power=-126;
    if(significand>=(UINT64_C(1)<<24)){significand>>=1;++power;}
    if(power>127)return sign|0x7f800000u;
    return sign|(significand<(UINT64_C(1)<<23)?0u:uint32_t(power+127)<<23)|((uint32_t)significand&0x7fffffu);
}
static inline uint64_t sqrt_strict(uint64_t bits) {
    uint64_t magnitude=bits&UINT64_C(0x7fffffffffffffff);
    if(!magnitude)return bits;
    if(magnitude>UINT64_C(0x7ff0000000000000))return bits|UINT64_C(0x8000000000000);
    if(bits>>63)return UINT64_C(0x7ff8000000000000);
    if(magnitude==UINT64_C(0x7ff0000000000000))return bits;
    int exponent=int(bits>>52)-1023;uint64_t mantissa=bits&UINT64_C(0xfffffffffffff);
    if(bits>>52)mantissa|=UINT64_C(1)<<52;
    else{unsigned top=top_bit(mantissa);mantissa<<=52u-top;exponent=-1022-int(52u-top);}
    unsigned shift=52u+(unsigned(exponent)&1u);
    uint64_t lo=mantissa<<shift,hi=mantissa>>(64u-shift),root=0,remainder=0;
    for(int digit=52;digit>=0;--digit) {
        unsigned source=unsigned(digit)*2u;
        uint64_t pair=source>=64u?(hi>>(source-64u))&3u:(lo>>source)&3u;
        remainder=(remainder<<2)|pair;
        uint64_t trial=(root<<2)|1u;root<<=1;
        if(remainder>=trial){remainder-=trial;root|=1u;}
    }
    if(remainder>root)++root;
    int result_exponent=(exponent-(exponent&1))/2;
    if(root==(UINT64_C(1)<<53)){root>>=1;++result_exponent;}
    return (uint64_t(result_exponent+1023)<<52)|(root&UINT64_C(0xfffffffffffff));
}
static inline bool dcp_domain(uint32_t op,uint64_t a,uint64_t b) {
    unsigned x=(unsigned)(a>>52)&2047u,y=(unsigned)(b>>52)&2047u;
    if(op==NARM_NUM_F64_SQRT)return !(a>>63)&&x>0u&&x<2047u;
    if(!x||!y||x==2047u||y==2047u)return false;
    if(op==NARM_NUM_F64_ADD||op==NARM_NUM_F64_SUB)return x>=54u&&y>=54u&&x<=2044u&&y<=2044u;
    if(op==NARM_NUM_F64_MUL)return x+y>=1024u&&x+y<=3067u;
    if(op==NARM_NUM_F64_DIV)return int(x)-int(y)>=-1021&&int(x)-int(y)<=1021;
    return false;
}
// Keep the opaque, side-effecting call between the priority-mask operations.
// SDK nesting protection alone cannot protect arbitrary non-LIFO task resumes.
static __attribute__((noinline,noipa)) uint64_t dcp_compute(uint32_t op,uint64_t a,uint64_t b) {
    __asm__ __volatile__("" ::: "memory");
    double x=as_double(a),y=as_double(b),result;
    if(op==NARM_NUM_F64_SQRT)result=sqrt(x);
    else if(op==NARM_NUM_F64_ADD)result=x+y;else if(op==NARM_NUM_F64_SUB)result=x-y;
    else if(op==NARM_NUM_F64_MUL)result=x*y;else result=x/y;
    uint64_t output=as_bits(result);
    __asm__ __volatile__("" : "+r"(output) :: "memory");
    return output;
}
static inline uint64_t arithmetic(uint32_t op,uint64_t a,uint64_t b) {
    bool fast=dcp_domain(op,a,b);
    if(fast) {
#if defined(__arm__) && defined(PICO_RP2350)
        uint32_t state=suspend_preemption();
        uint64_t output=dcp_compute(op,a,b);
        resume_preemption(state);return output;
#else
        return dcp_compute(op,a,b);
#endif
    }
    if(op==NARM_NUM_F64_SQRT)return sqrt_strict(a);
    if(op==NARM_NUM_F64_ADD)return add_strict(a,b);
    if(op==NARM_NUM_F64_SUB) {
        if((a&UINT64_C(0x7fffffffffffffff))>UINT64_C(0x7ff0000000000000)||
           (b&UINT64_C(0x7fffffffffffffff))>UINT64_C(0x7ff0000000000000))return quiet_nan(a,b);
        return add_strict(a,b^UINT64_C(0x8000000000000000));
    }
    if(op==NARM_NUM_F64_MUL)return multiply_strict(a,b);
    return divide_strict(a,b);
}
static __attribute__((noinline)) bool execute(uint32_t op,uint32_t* record) {
    if(op>=NARM_NUM_OPERATION_COUNT)return false;
    uint64_t a=uint64_t(record[0])|(uint64_t(record[1])<<32),
             b=uint64_t(record[2])|(uint64_t(record[3])<<32),out=0;
    if(op<=NARM_NUM_F64_SQRT)out=arithmetic(op,a,b);
    else if(op<=NARM_NUM_F64_GE) {
        uint64_t aa=a&UINT64_C(0x7fffffffffffffff),bb=b&UINT64_C(0x7fffffffffffffff);
        bool nan=aa>UINT64_C(0x7ff0000000000000)||bb>UINT64_C(0x7ff0000000000000);
        bool equal=a==b||(!aa&&!bb);
        uint64_t ak=(a>>63)?~a:a|UINT64_C(0x8000000000000000),bk=(b>>63)?~b:b|UINT64_C(0x8000000000000000);
        bool less=!equal&&ak<bk,greater=!equal&&ak>bk;
        if(op==NARM_NUM_F64_NE)out=nan||!equal;
        else if(!nan)out=op==NARM_NUM_F64_EQ?equal:op==NARM_NUM_F64_LT?less:
            op==NARM_NUM_F64_LE?(less||equal):op==NARM_NUM_F64_GT?greater:(greater||equal);
    } else switch(op) {
        case NARM_NUM_F32_TO_F64:out=widen_float((uint32_t)a);break;
        case NARM_NUM_I32_TO_F64:out=integer_float((uint64_t)(int64_t)(int32_t)a,true,false);break;
        case NARM_NUM_U32_TO_F64:out=integer_float((uint32_t)a,false,false);break;
        case NARM_NUM_I64_TO_F64:out=integer_float(a,true,false);break;
        case NARM_NUM_U64_TO_F64:out=integer_float(a,false,false);break;
        case NARM_NUM_F64_TO_F32:out=narrow_double(a);break;
        case NARM_NUM_F64_TO_I32:out=float_integer(a,false,32,false);break;
        case NARM_NUM_F64_TO_U32:out=float_integer(a,false,32,true);break;
        case NARM_NUM_F64_TO_I64:out=float_integer(a,false,64,false);break;
        case NARM_NUM_F64_TO_U64:out=float_integer(a,false,64,true);break;
        case NARM_NUM_I64_TO_F32:out=integer_float(a,true,true);break;
        case NARM_NUM_U64_TO_F32:out=integer_float(a,false,true);break;
        case NARM_NUM_F32_TO_I64:out=float_integer((uint32_t)a,true,64,false);break;
        case NARM_NUM_F32_TO_U64:out=float_integer((uint32_t)a,true,64,true);break;
        case NARM_NUM_U64_DIV:out=b?a/b:0;break;
        case NARM_NUM_U64_MOD:out=b?a%b:a;break;
        case NARM_NUM_I64_DIV:case NARM_NUM_I64_MOD:
            if(!b)out=op==NARM_NUM_I64_DIV?0:a;
            else if(a==UINT64_C(0x8000000000000000)&&b==UINT64_MAX)out=op==NARM_NUM_I64_DIV?a:0;
            else out=op==NARM_NUM_I64_DIV?(uint64_t)((int64_t)a/(int64_t)b):(uint64_t)((int64_t)a%(int64_t)b);
            break;
    }
    record[4]=(uint32_t)out;record[5]=(uint32_t)(out>>32);return true;
}
}
#endif
