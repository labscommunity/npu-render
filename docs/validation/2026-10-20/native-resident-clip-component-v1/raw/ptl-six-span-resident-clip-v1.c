/* Bounded NPU geometry component: exact fixed-point coverage and four FP32
 * varying registers. Input is already NPU-projected geometry. The host supplies
 * triangle IDs and sample positions; all coverage and interpolation are here.
 */
#pragma STDC FP_CONTRACT OFF
#define INLINE static __attribute__((always_inline)) inline
typedef float float4 __attribute__((ext_vector_type(4)));
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
INLINE float f32(const unsigned char *p){union{unsigned u;float f;}v;v.u=u32(p);return v.f;}
INLINE unsigned bits(float f){union{unsigned u;float f;}v;v.f=f;return v.u;}
INLINE int bounded(int v){return v>=-268435455 && v<=268435455;}
INLINE int finite(float f){return (bits(f)&0x7f800000u)!=0x7f800000u;}
INLINE long long edge(int ax,int ay,int bx,int by,int px,int py){return (long long)(bx-ax)*(py-ay)-(long long)(by-ay)*(px-ax);}
INLINE float weight(float ax,float ay,float bx,float by,float px,float py){return (bx-ax)*(py-ay)-(by-ay)*(px-ax);}

INLINE float clip_float(float v,float hi){return v<0?0:(v>hi?hi:v);}
INLINE float flush(float v){return (bits(v)&0x7f800000u)?v:0.0f;}
INLINE float4 texel(const unsigned char *p,unsigned format){
 uint4 raw=((uint4)u32(p)>>(uint4){0,8,16,24})&(uint4)255;
 float4 v=__builtin_convertvector(raw,float4)/255.0f;return format==87?v.zyxw:v;
}
INLINE float4 mix4(float4 a,float4 b,float t){return a*(1.0f-t)+b*t;}
INLINE unsigned quantize(float v){v=clip_float(v,255.0f);unsigned b=(unsigned)v;float f=v-(float)b;return b+(f>.5f||(f==.5f&&(b&1)));}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=131072||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;

 unsigned count=u32(data+8),vertices=u32(data+12),cull=u32(data+16),ccw=u32(data+20);
 unsigned target=u32(data+48),mask=u32(data+52),blend=u32(data+56);
 if(u32(data)!=0x4e524231||u32(data+4)!=1||count>16384||!vertices||vertices>2046||vertices%3||cull>2||ccw>1||u32(data+24)!=262144||u32(data+28)||(target!=28&&target!=87)||!mask||mask>15||blend>1||u32(data+60))return;
 int clip[4];unsigned mode[4]={2,2,2,0};
 for(unsigned n=0;n<4;++n){clip[n]=(int)u32(data+32+n*4);if(clip[n]<-268435455||clip[n]>268435456)return;}
 if(clip[0]>clip[2]||clip[1]>clip[3])return;
 unsigned widths[2],heights[2],formats[2],filters[2],offsets[2],end=256+vertices*96;
 for(unsigned n=0;n<2;++n){
  widths[n]=u32(data+64+n*12);heights[n]=u32(data+68+n*12);formats[n]=u32(data+72+n*12);filters[n]=u32(data+88+n*4);
  if(!widths[n]||widths[n]>8192||!heights[n]||heights[n]>8192||widths[n]*heights[n]>8192||(formats[n]!=28&&formats[n]!=87)||filters[n]>1)return;
  offsets[n]=end;end+=widths[n]*heights[n]*4;
  for(unsigned c=0;c<4;++c)if(!finite(f32(data+96+n*16+c*4)))return;
 }
 if(end>262144||count>(262144-end)/16)return;unsigned task_offset=end;
 for(unsigned n=0;n<count;++n){const unsigned char *task=data+task_offset+n*16;unsigned base=u32(task);if(base%3||base>vertices-3||!bounded((int)u32(task+4))||!bounded((int)u32(task+8)))return;}
 for(unsigned v=0;v<vertices;++v){const unsigned char *p=data+256+v*96;
  if(!bounded((int)u32(p))||!bounded((int)u32(p+4))||!finite(f32(p+8))||!finite(f32(p+12))||!(f32(p+8)>=-1048576.0f&&f32(p+8)<=1048576.0f&&f32(p+12)>=-1048576.0f&&f32(p+12)<=1048576.0f)||!(f32(p+16)>0&&f32(p+16)<=65536)||u32(p+20)||u32(p+24)||u32(p+28))return;
  for(unsigned n=0;n<16;++n)if(!(f32(p+32+n*4)>=-16384&&f32(p+32+n*4)<=16384))return;
 }
 unsigned hits=0;
 #pragma clang loop unroll(disable)
 for(unsigned n=0;n<count;++n){const unsigned char *task=data+task_offset+n*16;unsigned base=u32(task);int px=(int)u32(task+4),py=(int)u32(task+8);
  if(base%3||base>vertices-3||!bounded(px)||!bounded(py))return;
  const unsigned char *v[3]={data+256+base*96,data+256+(base+1)*96,data+256+(base+2)*96};int x[3],y[3];
  for(unsigned a=0;a<3;++a){x[a]=(int)u32(v[a]);y[a]=(int)u32(v[a]+4);}
  long long area=edge(x[0],y[0],x[1],y[1],x[2],y[2]);
  int covered=area!=0&&px>=clip[0]&&px<clip[2]&&py>=clip[1]&&py<clip[3];
  if(cull)covered=covered&&(((cull==2)!=ccw)?area>0:area<0);
  for(unsigned a=0;a<3;++a){unsigned b=(a+1)%3;int dx=x[b]-x[a],dy=y[b]-y[a];long long e=edge(x[a],y[a],x[b],y[b],px,py);
   int top=dy<0||(dy==0&&dx>0),reverse=dy>0||(dy==0&&dx<0);
   covered=covered&&(area>0?(e>0||(e==0&&top)):(e<0||(e==0&&reverse)));
  }
  unsigned background=u32(task+12);((unsigned*)(output+16))[n]=background;
  if(!covered)continue;
  unsigned result[20]={0};++hits;
  float sx=(float)px*(1.0f/256.0f),sy=(float)py*(1.0f/256.0f),w[3];
  for(unsigned a=0;a<3;++a){unsigned b=(a+1)%3,c=(a+2)%3;w[a]=weight(f32(v[b]+8),f32(v[b]+12),f32(v[c]+8),f32(v[c]+12),sx,sy);}
  for(unsigned attribute=0;attribute<4;++attribute){if(!mode[attribute])continue;
   float weights[3];for(unsigned a=0;a<3;++a)weights[a]=mode[attribute]==2?w[a]*f32(v[a]+16):w[a];
   float denominator=(weights[0]+weights[1])+weights[2];denominator=denominator+(denominator==0?1.0f:0.0f);
   for(unsigned c=0;c<4;++c){float value=f32(v[0]+32+attribute*16+c*4);
    if(mode[attribute]!=1){float sum=(weights[0]*value+weights[1]*f32(v[1]+32+attribute*16+c*4))+weights[2]*f32(v[2]+32+attribute*16+c*4);value=sum/denominator;}
    if(!finite(value))return;result[4+attribute*4+c]=bits(value);
   }
  }
  float4 samples[2];float enabled[2];
  for(unsigned sample=0;sample<2;++sample){
   unsigned width=widths[sample],height=heights[sample];union{unsigned u;float f;}ux,uy,uz;ux.u=result[8+sample*4];uy.u=result[9+sample*4];uz.u=result[10+sample*4];
   float u=ux.f/uz.f,v=uy.f/uz.f;if(!finite(u)||!finite(v))return;
   const unsigned char *bounds=data+96+sample*16;
   enabled[sample]=(flush(u)>=flush(f32(bounds))&&flush(v)>=flush(f32(bounds+4))&&flush(f32(bounds+8))>=flush(u)&&flush(f32(bounds+12))>=flush(v))?1.0f:0.0f;
   float half=filters[sample]?0.0f:0.5f;
   float x=clip_float(clip_float(u,1.0f)*(float)width-half,(float)(width-1));
   float y=clip_float(clip_float(v,1.0f)*(float)height-half,(float)(height-1));
   unsigned ix=(unsigned)x,iy=(unsigned)y,jx=ix+1<width?ix+1:ix,jy=iy+1<height?iy+1:iy;
   const unsigned char *texture=data+offsets[sample];float4 a=texel(texture+(iy*width+ix)*4,formats[sample]);
   if(filters[sample])samples[sample]=a;
   else samples[sample]=mix4(mix4(a,texel(texture+(iy*width+jx)*4,formats[sample]),x-(float)ix),mix4(texel(texture+(jy*width+ix)*4,formats[sample]),texel(texture+(jy*width+jx)*4,formats[sample]),x-(float)ix),y-(float)iy);
  }
  union{unsigned u;float f;}alpha;alpha.u=result[7];
  float4 color=((1.0f-enabled[1]*samples[1].w)*(enabled[0]*samples[0]))*alpha.f;
  for(unsigned c=0;c<4;++c){if(!finite(color[c]))return;color[c]=clip_float(color[c],1.0f);}
  float source_alpha=color.w;if(target==87)color=color.zyxw;unsigned packed=background;
  for(unsigned c=0;c<4;++c){unsigned logical=target==87&&c<3?2-c:c;if(!(mask&(1u<<logical)))continue;
   float v=color[c]*255.0f;if(blend)v=v+(float)((background>>(c*8))&255u)*(1.0f-source_alpha);
   packed=(packed&~(255u<<(c*8)))|(quantize(v)<<(c*8));
  }
  ((unsigned*)(output+16))[n]=packed;
 }
 unsigned *header=(unsigned*)output;header[1]=count;header[2]=hits;header[3]=0;header[0]=0x4e52424f;
}
