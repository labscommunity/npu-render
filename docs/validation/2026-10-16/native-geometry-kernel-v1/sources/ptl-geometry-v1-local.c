/* Bounded NPU geometry component: exact fixed-point coverage and four FP32
 * varying registers. Input is already NPU-projected geometry. The host supplies
 * triangle IDs and sample positions; all coverage and interpolation are here.
 */
#pragma STDC FP_CONTRACT OFF
#define INLINE static __attribute__((always_inline)) inline
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
INLINE unsigned u32(const unsigned char *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
INLINE float f32(const unsigned char *p){union{unsigned u;float f;}v;v.u=u32(p);return v.f;}
INLINE unsigned bits(float f){union{unsigned u;float f;}v;v.f=f;return v.u;}
INLINE int bounded(int v){return v>=-268435455 && v<=268435455;}
INLINE int finite(float f){return (bits(f)&0x7f800000u)!=0x7f800000u;}
INLINE long long edge(int ax,int ay,int bx,int by,int px,int py){return (long long)(bx-ax)*(py-ay)-(long long)(by-ay)*(px-ax);}
INLINE float weight(float ax,float ay,float bx,float by,float px,float py){return (bx-ax)*(py-ay)-(by-ay)*(px-ax);}
void nr_geometry_local(const unsigned char *data,unsigned char *output){
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;
 unsigned count=u32(data+8),vertices=u32(data+12),cull=u32(data+16),ccw=u32(data+20);
 if(u32(data)!=0x4e524731||u32(data+4)!=1||count>3072||!vertices||vertices>252||vertices%3||cull>2||ccw>1||u32(data+24)!=262144||u32(data+28))return;
 int clip[4];unsigned mode[4];
 for(unsigned n=0;n<4;++n){clip[n]=(int)u32(data+32+n*4);mode[n]=u32(data+48+n*4);if(clip[n]<-268435455||clip[n]>268435456||(mode[n]!=0&&mode[n]!=1&&mode[n]!=2&&mode[n]!=4))return;}
 if(clip[0]>clip[2]||clip[1]>clip[3])return;
 for(unsigned v=0;v<vertices;++v){const unsigned char *p=data+256+v*96;
  if(!bounded((int)u32(p))||!bounded((int)u32(p+4))||!finite(f32(p+8))||!finite(f32(p+12))||!(f32(p+8)>=-1048576.0f&&f32(p+8)<=1048576.0f&&f32(p+12)>=-1048576.0f&&f32(p+12)<=1048576.0f)||!(f32(p+16)>0&&f32(p+16)<=65536)||u32(p+20)||u32(p+24)||u32(p+28))return;
  for(unsigned n=0;n<16;++n)if(!(f32(p+32+n*4)>=-16384&&f32(p+32+n*4)<=16384))return;
 }
 unsigned hits=0;
 #pragma clang loop unroll(disable)
 for(unsigned n=0;n<count;++n){const unsigned char *task=data+24576+n*12;unsigned base=u32(task);int px=(int)u32(task+4),py=(int)u32(task+8);
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
 }
 unsigned *header=(unsigned*)output;header[1]=count;header[2]=hits;header[3]=0;header[0]=0x4e52474f;
}
