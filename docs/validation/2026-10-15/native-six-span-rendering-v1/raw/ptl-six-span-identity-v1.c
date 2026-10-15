/* Exact input/output descriptors and six scheduler spans are admitted by host. */
#define INLINE static __attribute__((always_inline)) inline
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
void nr_probe_kernel(const unsigned char *params){
    if(!params)return;
    for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
        if(!u32(p) || u32(p+4) || u32(p+8)!=1 || u32(p+12)!=4 || !u32(p+16) || !u32(p+20) || u32(p+24)!=2 || u32(p+28)!=0x4321 || u32(p+32) || u32(p+36)!=2)return;
        const unsigned *dims=(const unsigned*)u32(p+16);
        if(dims[0]!=131072 || dims[1]!=1 || dims[2]!=1 || dims[3]!=1)return;
    }
    const uint4 *source=(const uint4*)u32(params);uint4 *output=(uint4*)u32(params+40);
    for(unsigned i=0;i<16384;++i)output[i]=source[i];
}
