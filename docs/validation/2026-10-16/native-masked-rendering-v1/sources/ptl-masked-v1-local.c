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
INLINE float value(unsigned u){union{unsigned u;float f;}v;v.u=u;return v.f;}
INLINE float clamp(float v,float hi){return v<0?0:(v>hi?hi:v);}
INLINE float4 texel(const unsigned char *p,unsigned format){
 uint4 raw=((uint4)u32(p)>>(uint4){0,8,16,24})&(uint4)255;
 float4 result=__builtin_convertvector(raw,float4)/255.0f;return format==87?result.zyxw:result;
}
INLINE float4 mix4(float4 a,float4 b,float t){return a*(1.0f-t)+b*t;}
INLINE unsigned quantize(float v){v=clamp(v,255.0f);unsigned b=(unsigned)v;float f=v-(float)b;return b+(f>.5f||(f==.5f&&(b&1)));}
void nr_masked_local(const unsigned char *data,unsigned char *output){
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;
 unsigned phase=u32(data+8),count=u32(data+12),vertices=u32(data+16),cull=u32(data+20),ccw=u32(data+24);
 unsigned target=u32(data+48),mask=u32(data+52),blend=u32(data+56),width[2],height[2],format[2];
 if(u32(data)!=0x4e524d31||u32(data+4)!=1||phase>1||count>3072||u32(data+28)!=262144||(target!=28&&target!=87)||!mask||mask>15||blend>1||u32(data+60))return;
 for(unsigned i=0;i<2;++i){width[i]=u32(data+64+i*12);height[i]=u32(data+68+i*12);format[i]=u32(data+72+i*12);if(!width[i]||width[i]>8192||!height[i]||height[i]>8192||width[i]*height[i]>4194304||(format[i]!=28&&format[i]!=87))return;}
 if(phase){
  if(vertices||cull||ccw)return;unsigned hits=0;
  #pragma clang loop unroll(disable)
  for(unsigned n=0;n<count;++n){const unsigned char *p=data+256+n*80;unsigned covered=u32(p+68),background=u32(p+64);if(covered>1||u32(p+72)||u32(p+76))return;
   if(!covered){((unsigned*)(output+16))[n]=background;continue;}++hits;
   float4 samples[2];
   for(unsigned sample=0;sample<2;++sample){float tx=f32(p+16+sample*8),ty=f32(p+20+sample*8);if(!(tx>=0&&tx<1&&ty>=0&&ty<1))return;
    const unsigned char *q=p+32+sample*16;samples[sample]=mix4(mix4(texel(q,format[sample]),texel(q+4,format[sample]),tx),mix4(texel(q+8,format[sample]),texel(q+12,format[sample]),tx),ty);
   }
   float4 color=(float4){f32(p),f32(p+4),f32(p+8),f32(p+12)};
   for(unsigned c=0;c<4;++c)if(!(color[c]>=-16384&&color[c]<=16384))return;
   color=(color*samples[0])*samples[1].w;
   for(unsigned c=0;c<4;++c){if(!(color[c]>=-16&&color[c]<=16))return;color[c]=clamp(color[c],1.0f);}
   float alpha=color.w;if(target==87)color=color.zyxw;unsigned packed=background;
   for(unsigned c=0;c<4;++c){unsigned logical=target==87&&c<3?2-c:c;if(!(mask&(1u<<logical)))continue;
    float result=color[c]*255.0f;if(blend)result=result+(float)((background>>(8*c))&255u)*(1.0f-alpha);
    packed=(packed&~(255u<<(c*8)))|(quantize(result)<<(c*8));
   }((unsigned*)(output+16))[n]=packed;
  }
  unsigned *header=(unsigned*)output;header[1]=1;header[2]=count;header[3]=hits;header[0]=0x4e524d4f;return;
 }
 if(!vertices||vertices>2046||vertices%3||cull>2||ccw>1)return;
 int clip[4];unsigned mode[4]={2,2,2,0};
 for(unsigned n=0;n<4;++n){clip[n]=(int)u32(data+32+n*4);if(clip[n]<-268435455||clip[n]>268435456)return;}
 if(clip[0]>clip[2]||clip[1]>clip[3])return;
 for(unsigned v=0;v<vertices;++v){const unsigned char *p=data+256+v*96;
  if(!bounded((int)u32(p))||!bounded((int)u32(p+4))||!finite(f32(p+8))||!finite(f32(p+12))||!(f32(p+8)>=-1048576.0f&&f32(p+8)<=1048576.0f&&f32(p+12)>=-1048576.0f&&f32(p+12)<=1048576.0f)||!(f32(p+16)>0&&f32(p+16)<=65536)||u32(p+20)||u32(p+24)||u32(p+28))return;
  for(unsigned n=0;n<16;++n)if(!(f32(p+32+n*4)>=-16384&&f32(p+32+n*4)<=16384))return;
 }
 unsigned hits=0;
 #pragma clang loop unroll(disable)
 for(unsigned n=0;n<count;++n){const unsigned char *task=data+196864+n*12;unsigned base=u32(task);int px=(int)u32(task+4),py=(int)u32(task+8);
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
  if(!covered)continue;
  unsigned *result=(unsigned*)(output+16+n*80);result[0]=1;++hits;
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
  float uv[4]={value(result[8]),value(result[9]),value(result[12]),value(result[13])};
  for(unsigned sample=0;sample<2;++sample){unsigned w=width[sample],h=height[sample];
   float x=clamp(clamp(uv[sample*2],1.0f)*(float)w-0.5f,(float)(w-1));
   float y=clamp(clamp(uv[sample*2+1],1.0f)*(float)h-0.5f,(float)(h-1));
   unsigned ix=(unsigned)x,iy=(unsigned)y,jx=ix+1<w?ix+1:ix,jy=iy+1<h?iy+1:iy;
   result[8+sample*4]=iy*w+ix;result[9+sample*4]=iy*w+jx;result[10+sample*4]=jy*w+ix;result[11+sample*4]=jy*w+jx;
   result[16+sample*2]=bits(x-(float)ix);result[17+sample*2]=bits(y-(float)iy);
  }
 }
 unsigned *header=(unsigned*)output;header[1]=0;header[2]=count;header[3]=hits;header[0]=0x4e524d4f;
}
