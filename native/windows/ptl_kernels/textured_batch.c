/* Isolated ordered solid/point-texture rectangle component.
 * Immutable texture windows are packed by the host; coverage, sampling,
 * format conversion and per-command quantized blending run here. */
#define INLINE static __attribute__((always_inline)) inline
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){
 return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
INLINE unsigned swap_rb(unsigned p){return (p&0xff00ff00u)|((p&255u)<<16)|((p>>16)&255u);}
INLINE unsigned shade(unsigned dest,unsigned source,unsigned write,unsigned blend){
 if(!blend)return (dest&~write)|(source&write);
 uint4 s=((uint4)source>>(uint4){0,8,16,24})&(uint4)255;
 uint4 d=((uint4)dest>>(uint4){0,8,16,24})&(uint4)255;
 uint4 v=s*(uint4)255+d*(uint4)(255-s.w);
 v=v>(uint4)65025?(uint4)65025:v;
 v=((v+(uint4)127)*(uint4)32897)>>(uint4)23;
 unsigned packed=v.x|(v.y<<8)|(v.z<<16)|(v.w<<24);
 return (dest&~write)|(packed&write);
}
INLINE uint4 shade4(uint4 dest,uint4 source,unsigned write,unsigned blend){
 if(!blend)return (dest&(uint4)~write)|(source&(uint4)write);
 uint4 result=dest,factor=(uint4)255-(source>>(uint4)24);
 #pragma clang loop unroll(full)
 for(unsigned c=0;c<4;++c){unsigned shift=c*8,mask=255u<<shift;if(!(write&mask))continue;
  uint4 v=((source>>(uint4)shift)&(uint4)255)*(uint4)255+((dest>>(uint4)shift)&(uint4)255)*factor;
  v=v>(uint4)65025?(uint4)65025:v;
  v=((v+(uint4)127)*(uint4)32897)>>(uint4)23;
  result=(result&(uint4)~mask)|(v<<(uint4)shift);
 }
 return result;
}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=132096||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *zero=(uint4*)output;for(unsigned n=0;n<16512;++n)zero[n]=(uint4)0;
 unsigned count=u32(data+8),first=u32(data+12),w=u32(data+16),h=u32(data+20),commands=u32(data+24),format=u32(data+28),atlas=u32(data+36);
 if(u32(data)!=0x4e525451||u32(data+4)!=1||count>16384||!w||w>8192||!h||h>8192||w*h>8388576||first>w*h||count>w*h-first||!commands||commands>32||(format!=28&&format!=87)||u32(data+32)!=264192||atlas>196544||atlas%4)return;
 for(unsigned n=40;n<64;n+=4)if(u32(data+n))return;
 /* Validate the complete batch before publishing even the background. */
 for(unsigned n=0;n<commands;++n){const unsigned char *p=data+64+n*64;
  unsigned x=u32(p),y=u32(p+4),right=u32(p+8),bottom=u32(p+12),kind=u32(p+28);
  if(x>right||y>bottom||right>w||bottom>h||u32(p+20)>1||!u32(p+24)||u32(p+24)>15||kind>1)return;
  if(kind){
   unsigned offset=u32(p+16),tw=u32(p+32),th=u32(p+36),sx=u32(p+40),sy=u32(p+44),tf=u32(p+48);
   if(offset%4||offset>atlas||!tw||tw>8192||!th||th>8192||tw*th>49136||tw*th*4>atlas-offset||(tf!=28&&tf!=87)||sx>tw||sy>th||right-x>tw-sx||bottom-y>th-sy)return;
  }else{for(unsigned k=32;k<52;k+=4)if(u32(p+k))return;}
  if(u32(p+52)||u32(p+56)||u32(p+60))return;
 }
 for(unsigned n=0;n<count;++n)((unsigned*)(output+16))[n]=u32(data+2112+n*4);
 unsigned hits=0;
 #pragma clang loop unroll(disable)
 for(unsigned c=0;c<commands;++c){const unsigned char *p=data+64+c*64;
  unsigned x0=u32(p),y0=u32(p+4),x1=u32(p+8),y1=u32(p+12),source=u32(p+16),blend=u32(p+20),mask=u32(p+24),kind=u32(p+28);
  if(x0==x1||y0==y1||!count)continue;
  unsigned tw=u32(p+32),sx=u32(p+40),sy=u32(p+44),tf=kind?u32(p+48):28;
  if(format==87)mask=(mask&10)|((mask&1)<<2)|((mask&4)>>2);
  unsigned write=(mask&1?255u:0u)|(mask&2?65280u:0u)|(mask&4?16711680u:0u)|(mask&8?4278190080u:0u);
  unsigned low=first/w,high=(first+count-1)/w+1;if(low<y0)low=y0;if(high>y1)high=y1;
  #pragma clang loop unroll(disable)
  for(unsigned y=low;y<high;++y){
   unsigned begin=y*w+x0,end=y*w+x1;if(begin<first)begin=first;if(end>first+count)end=first+count;
   if(begin>=end)continue;hits+=end-begin;
   unsigned at=begin;
   for(;at<end&&((at-first)&3);++at){
    unsigned packed=source;if(kind)packed=u32(data+67648+source+((sy+y-y0)*tw+sx+at-y*w-x0)*4);
    if(tf!=format)packed=swap_rb(packed);
    unsigned *pixel=(unsigned*)(output+16)+(at-first);*pixel=shade(*pixel,packed,write,blend);
   }
   #pragma clang loop unroll(disable)
   for(;end-at>=4;at+=4){
    uint4 packed=(uint4)source;
    if(kind){const unsigned char *t=data+67648+source+((sy+y-y0)*tw+sx+at-y*w-x0)*4;packed=(uint4){u32(t),u32(t+4),u32(t+8),u32(t+12)};}
    if(tf!=format)packed=(packed&(uint4)0xff00ff00u)|((packed&(uint4)255)<<(uint4)16)|((packed>>(uint4)16)&(uint4)255);
    uint4 *pixel=(uint4*)(output+16+(at-first)*4);*pixel=shade4(*pixel,packed,write,blend);
   }
   for(;at<end;++at){
    unsigned packed=source;if(kind)packed=u32(data+67648+source+((sy+y-y0)*tw+sx+at-y*w-x0)*4);
    if(tf!=format)packed=swap_rb(packed);
    unsigned *pixel=(unsigned*)(output+16)+(at-first);*pixel=shade(*pixel,packed,write,blend);
   }

  }
 }
 unsigned *header=(unsigned*)output;header[1]=count;header[2]=commands;header[3]=hits;header[0]=0x4e52544f;
}
