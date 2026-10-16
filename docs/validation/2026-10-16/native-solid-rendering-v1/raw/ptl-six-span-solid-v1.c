/* Experimental bounded multi-rectangle renderer on six pinned NPU50XX spans.
 * Raw f16 storage transports packed bytes. Coverage, blending, quantization,
 * and overlap ordering execute on NPU. No host rasterization or device control.
 */
#pragma STDC FP_CONTRACT OFF
#define INLINE static __attribute__((always_inline)) inline
typedef float float4 __attribute__((ext_vector_type(4)));
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
INLINE float f32(const unsigned char *p){union{unsigned u;float f;}v;v.u=u32(p);return v.f;}
INLINE unsigned rounded(float v){v=v<0?0:(v>255?255:v);unsigned base=(unsigned)v;float frac=v-(float)base;return base+(frac>.5f || (frac==.5f && (base&1)));}
INLINE uint4 packed(const unsigned char *p){return ((uint4)u32(p)>>(uint4){0,8,16,24})&(uint4)255;}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=131072||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;
 unsigned w=u32(data+8),h=u32(data+12),first=u32(data+16),count=u32(data+20),rectangles=u32(data+24),blend=u32(data+28),format=u32(data+32),mask=u32(data+36);
 if(u32(data)!=0x4e525332||u32(data+4)!=1||!w||w>8192||!h||h>8192||first>w*h||count>65280||count>w*h-first||rectangles>28||blend>1||(format!=28&&format!=87)||!mask||mask>15||u32(data+56)||u32(data+60)!=262144)return;
 int clip[4];for(unsigned c=0;c<4;++c)clip[c]=(int)u32(data+40+c*4);
 if(clip[0]<0||clip[1]<0||clip[2]<clip[0]||clip[3]<clip[1]||clip[2]>(int)w*256||clip[3]>(int)h*256)return;
 for(unsigned n=0;n<rectangles;++n){const unsigned char *p=data+64+n*32;
  for(unsigned c=0;c<4;++c){int b=(int)u32(p+c*4);float color=f32(p+16+c*4);if(b<-8388608||b>8388608||!(color>=0&&color<=1))return;}
  if((int)u32(p)>(int)u32(p+8)||(int)u32(p+4)>(int)u32(p+12))return;
 }
 unsigned px=first%w,py=first/w,hits=0;
 #pragma clang loop unroll(disable)
 for(unsigned i=0;i<count;++i){
  int sx=(int)px*256+128,sy=(int)py*256+128;uint4 bg=packed(data+1024+i*4);
  if(sx>=clip[0]&&sx<clip[2]&&sy>=clip[1]&&sy<clip[3]){
   for(unsigned n=0;n<rectangles;++n){const unsigned char *p=data+64+n*32;
    if(sx>=(int)u32(p)&&sy>=(int)u32(p+4)&&sx<(int)u32(p+8)&&sy<(int)u32(p+12)){
     ++hits;float4 color=(float4){f32(p+16),f32(p+20),f32(p+24),f32(p+28)};float alpha=color.w;if(format==87)color=color.zyxw;
     float4 value=color*255.0f;if(blend)value=value+__builtin_convertvector(bg,float4)*(1.0f-alpha);
     #pragma clang loop unroll(full)
     for(unsigned c=0;c<4;++c){unsigned logical=format==87&&c<3?2-c:c;if(mask&(1u<<logical))bg[c]=rounded(value[c]);}
    }
   }
  }
  unsigned word=bg.x|(bg.y<<8)|(bg.z<<16)|(bg.w<<24);((unsigned*)(output+16))[i]=word;
  ++px;if(px==w){px=0;++py;}
 }
 unsigned *header=(unsigned*)output;header[1]=count;header[2]=hits;header[3]=0;header[0]=0x4e52534f;
}
