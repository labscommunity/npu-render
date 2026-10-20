/* Experimental ordered RGBA8 rectangle batch. Each pixel retains its packed
 * destination across commands; coverage and blending execute on the NPU. */
#define INLINE static __attribute__((always_inline)) inline
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=131072||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;
 unsigned count=u32(data+8),first=u32(data+12),w=u32(data+16),h=u32(data+20),commands=u32(data+24),format=u32(data+28);
 if(u32(data)!=0x4e524351||u32(data+4)!=1||count>65264||!w||w>8192||!h||h>8192||w*h>8388576||first>w*h||count>w*h-first||!commands||commands>32||(format!=28&&format!=87)||u32(data+32)!=262144)return;
 for(unsigned n=36;n<64;n+=4)if(u32(data+n))return;
 for(unsigned n=0;n<commands;++n){const unsigned char *p=data+64+n*32;
  if(u32(p)>u32(p+8)||u32(p+4)>u32(p+12)||u32(p+8)>w||u32(p+12)>h||u32(p+20)>1||!u32(p+24)||u32(p+24)>15||u32(p+28))return;
 }
 unsigned hits=0,x=first%w,y=first/w;
 #pragma clang loop unroll(disable)
 for(unsigned n=0;n<count;++n){
  unsigned packed=u32(data+1088+n*4);
  uint4 destination=((uint4)packed>>(uint4){0,8,16,24})&(uint4)255;
  #pragma clang loop unroll(disable)
  for(unsigned c=0;c<commands;++c){const unsigned char *p=data+64+c*32;
   if(x<u32(p)||y<u32(p+4)||x>=u32(p+8)||y>=u32(p+12))continue;
   ++hits;unsigned source=u32(p+16),mask=u32(p+24);
   uint4 value=((uint4)source>>(uint4){0,8,16,24})&(uint4)255;
   if(format==87){value=value.zyxw;mask=(mask&10)|((mask&1)<<2)|((mask&4)>>2);}
   if(u32(p+20)){
    uint4 numerator=value*(uint4)255+destination*(uint4)(255-value.w);
    numerator=numerator>(uint4)65025?(uint4)65025:numerator;
    value=((numerator+(uint4)127)*(uint4)32897)>>(uint4)23;
   }
   if(mask&1)destination.x=value.x;if(mask&2)destination.y=value.y;
   if(mask&4)destination.z=value.z;if(mask&8)destination.w=value.w;
  }
  ((unsigned*)(output+16))[n]=destination.x|(destination.y<<8)|(destination.z<<16)|(destination.w<<24);
  ++x;if(x==w){x=0;++y;}
 }
 unsigned *header=(unsigned*)output;header[1]=count;header[2]=commands;header[3]=hits;header[0]=0x4e52434f;
}
