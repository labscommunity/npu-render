/* Experimental ordered RGBA8 rectangle batch. Each pixel retains its packed
 * destination across commands; coverage and blending execute on the NPU. */
#define INLINE static __attribute__((always_inline)) inline
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
INLINE unsigned single_pixel(unsigned background,unsigned packed,unsigned write,unsigned blend){
 if(!blend)return (background&~write)|(packed&write);
 uint4 value=((uint4)packed>>(uint4){0,8,16,24})&(uint4)255;
 uint4 dest=((uint4)background>>(uint4){0,8,16,24})&(uint4)255;
 uint4 numerator=value*(uint4)255+dest*(uint4)(255-value.w);
 numerator=numerator>(uint4)65025?(uint4)65025:numerator;
 uint4 result=((numerator+(uint4)127)*(uint4)32897)>>(uint4)23;
 unsigned shaded=result.x|(result.y<<8)|(result.z<<16)|(result.w<<24);
 return (background&~write)|(shaded&write);
}
INLINE uint4 four_pixels(uint4 background,unsigned packed,unsigned write,unsigned blend){
 if(!blend)return (background&(uint4)~write)|((uint4)packed&(uint4)write);
 uint4 result=background;unsigned factor=255-(packed>>24);
 #pragma clang loop unroll(full)
 for(unsigned c=0;c<4;++c){unsigned shift=c*8,mask=255u<<shift;if(!(write&mask))continue;
  uint4 numerator=(uint4)(((packed>>shift)&255u)*255u)+((background>>(uint4)shift)&(uint4)255)*(uint4)factor;
  numerator=numerator>(uint4)65025?(uint4)65025:numerator;
  uint4 channel=((numerator+(uint4)127)*(uint4)32897)>>(uint4)23;
  result=(result&(uint4)~mask)|(channel<<(uint4)shift);
 }
 return result;
}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=264192||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *words=(uint4*)output;for(unsigned n=0;n<33024;++n)words[n]=(uint4)0;
 unsigned count=u32(data+8),first=u32(data+12),w=u32(data+16),h=u32(data+20),commands=u32(data+24),format=u32(data+28);
 if(u32(data)!=0x4e524351||u32(data+4)!=1||count>131824||!w||w>8192||!h||h>8192||w*h>8388576||first>w*h||count>w*h-first||!commands||commands>32||(format!=28&&format!=87)||u32(data+32)!=528384)return;
 for(unsigned n=36;n<64;n+=4)if(u32(data+n))return;
 for(unsigned n=0;n<commands;++n){const unsigned char *p=data+64+n*32;
  if(u32(p)>u32(p+8)||u32(p+4)>u32(p+12)||u32(p+8)>w||u32(p+12)>h||u32(p+20)>1||!u32(p+24)||u32(p+24)>15||u32(p+28))return;
 }
 unsigned hits=0;
 // Preserve the destination in NPU-visible output storage between commands.
 // Traverse only covered row intervals; application order is unchanged.
 for(unsigned n=0;n<count;++n)((unsigned*)(output+16))[n]=u32(data+1088+n*4);
 #pragma clang loop unroll(disable)
 for(unsigned c=0;c<commands;++c){const unsigned char *p=data+64+c*32;
  unsigned x0=u32(p),y0=u32(p+4),x1=u32(p+8),y1=u32(p+12),source=u32(p+16),blend=u32(p+20),mask=u32(p+24);
  if(x0==x1||y0==y1||!count)continue;
  uint4 value=((uint4)source>>(uint4){0,8,16,24})&(uint4)255;
  if(format==87){value=value.zyxw;mask=(mask&10)|((mask&1)<<2)|((mask&4)>>2);}
  unsigned packed=value.x|(value.y<<8)|(value.z<<16)|(value.w<<24);
  unsigned write=(mask&1?255u:0u)|(mask&2?65280u:0u)|(mask&4?16711680u:0u)|(mask&8?4278190080u:0u);
  unsigned low=first/w,high=(first+count-1)/w+1;if(low<y0)low=y0;if(high>y1)high=y1;
  if(value.w==255)blend=0;
  #pragma clang loop unroll(disable)
  for(unsigned y=low;y<high;++y){
   unsigned begin=y*w+x0,end=y*w+x1;if(begin<first)begin=first;if(end>first+count)end=first+count;
   if(begin>=end)continue;hits+=end-begin;
   if(blend&&!packed)continue;
   unsigned at=begin;
   while(at<end&&((at-first)&3)){unsigned *pixel=(unsigned*)(output+16)+(at-first);*pixel=single_pixel(*pixel,packed,write,blend);++at;}
   #pragma clang loop unroll(disable)
   for(;end-at>=4;at+=4){uint4 *pixel=(uint4*)(output+16+(at-first)*4);
    if(!blend&&write==0xffffffffu)*pixel=(uint4)packed;
    else *pixel=four_pixels(*pixel,packed,write,blend);
   }
   for(;at<end;++at){unsigned *pixel=(unsigned*)(output+16)+(at-first);*pixel=single_pixel(*pixel,packed,write,blend);}

  }
 }
 unsigned *header=(unsigned*)output;header[1]=count;header[2]=commands;header[3]=hits;header[0]=0x4e52434f;
}
