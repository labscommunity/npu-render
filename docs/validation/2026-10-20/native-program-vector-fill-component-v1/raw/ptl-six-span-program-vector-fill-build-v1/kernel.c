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

INLINE int source_valid(unsigned code,unsigned mask,const unsigned *initialized,const unsigned *mode){
 if(code&0xffff000cu)return 0;unsigned kind=code&3,index=(code>>4)&15;
 if(kind==0){if(index>=3||!mode[index])return 0;}
 else if(kind==1){if(index>=4)return 0;for(unsigned c=0;c<4;++c)if((mask&(1u<<c))&&!(initialized[index]&(1u<<((code>>(8+c*2))&3))))return 0;}
 else if(kind==2){if(index>=4)return 0;}else return 0;return 1;
}
INLINE float4 source_value(unsigned code,const float4 *varyings,const float4 *temporaries,const float4 *immediates){
 unsigned kind=code&3,index=(code>>4)&15;float4 raw=kind==0?varyings[index]:(kind==1?temporaries[index]:immediates[index]);
 return (float4){raw[(code>>8)&3],raw[(code>>10)&3],raw[(code>>12)&3],raw[(code>>14)&3]};
}
void nr_probe_kernel(const unsigned char *params){
 if(!params)return;
 for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
  if(!u32(p)||u32(p+4)||u32(p+8)!=1||u32(p+12)!=4||!u32(p+16)||!u32(p+20)||u32(p+24)!=2||u32(p+28)!=0x4321||u32(p+32)||u32(p+36)!=2)return;
  const unsigned *dims=(const unsigned*)u32(p+16);if(dims[0]!=131072||dims[1]!=1||dims[2]!=1||dims[3]!=1)return;
 }
 const unsigned char *data=(const unsigned char*)u32(params);unsigned char *output=(unsigned char*)u32(params+40);
 uint4 *words=(uint4*)output;for(unsigned n=0;n<16384;++n)words[n]=(uint4)0;
 unsigned phase=u32(data+8),count=u32(data+12),vertices=u32(data+16),cull=u32(data+20),ccw=u32(data+24);
 unsigned target=u32(data+48),mask=u32(data+52),blend=u32(data+56),width[2],height[2],format[2];
 if(u32(data)!=0x4e525031||u32(data+4)!=3||phase>1||count>16384||u32(data+28)!=262144||(target!=28&&target!=87)||!mask||mask>15||blend>1||u32(data+60))return;
 unsigned filters[2]={u32(data+144),u32(data+148)};if(filters[0]>1||filters[1]>1||u32(data+152)||u32(data+156))return;
 unsigned mode[4]={u32(data+88),u32(data+92),u32(data+96),0},samples_count=u32(data+100),instructions=u32(data+104);
 if(samples_count>2||!instructions||instructions>8||u32(data+108))return;
 for(unsigned i=0;i<3;++i)if(mode[i]!=0&&mode[i]!=1&&mode[i]!=2&&mode[i]!=4)return;
 for(unsigned i=0;i<samples_count;++i){width[i]=u32(data+64+i*12);height[i]=u32(data+68+i*12);format[i]=u32(data+72+i*12);
  if(!width[i]||width[i]>8192||!height[i]||height[i]>8192||width[i]*height[i]>4194304||(format[i]!=28&&format[i]!=87))return;
  const unsigned char *desc=data+112+i*16;unsigned slot=u32(desc),x=u32(desc+4),y=u32(desc+8),divide=u32(desc+12);
  if(slot>=3||!mode[slot]||x>3||y>3||divide>4)return;
 }
 float4 immediates[4];for(unsigned i=0;i<4;++i)for(unsigned c=0;c<4;++c){float v=f32(data+160+i*16+c*4);if(!(v>=-16384&&v<=16384))return;immediates[i][c]=v;}
 unsigned initialized[5]={0,0,0,0,0},live=0;
 for(unsigned i=0;i<instructions;++i){const unsigned char *ins=data+256+i*32;unsigned op=u32(ins),dest=u32(ins+4),reg=dest&15,write=(dest>>4)&15;
  if(op>3||dest>255||reg>4||!write||u32(ins+16)||u32(ins+20)||u32(ins+24)||u32(ins+28))return;
  if(op==3){if(u32(ins+8)>=samples_count||u32(ins+12))return;}
  else {if(!source_valid(u32(ins+8),write,initialized,mode))return;if(op){if(!source_valid(u32(ins+12),write,initialized,mode))return;}else if(u32(ins+12))return;}
  if(op!=3){for(unsigned source=0;source<(op?2u:1u);++source){unsigned code=u32(ins+8+source*4);if((code&3u)==0)live|=1u<<((code>>4)&15u);}}
  initialized[reg]|=write;
 }
 if(initialized[4]!=15)return;
 unsigned offsets[3]={0,0,0},sample_offsets[2]={0,0},stride=4;
 for(unsigned a=0;a<3;++a)if(live&(1u<<a)){offsets[a]=stride;stride+=16;}
 for(unsigned a=0;a<samples_count;++a){sample_offsets[a]=stride;stride+=filters[a]?4:24;}
 if(count>(262144u-16u)/stride||count>(262144u-2048u)/(stride+4u))return;

 if(phase){
  if(vertices||cull||ccw)return;unsigned hits=0;
  #pragma clang loop unroll(disable)
  for(unsigned n=0;n<count;++n){const unsigned char *p=data+2048+n*(stride+4);unsigned covered=u32(p),background=u32(p+4);if(covered>1)return;
   if(!covered){((unsigned*)(output+16))[n]=background;continue;}++hits;
   float4 samples[2]={(float4)0,(float4)0},varyings[3]={(float4)0,(float4)0,(float4)0},registers[5]={(float4)0,(float4)0,(float4)0,(float4)0,(float4)0};
   for(unsigned a=0;a<3;++a)if(live&(1u<<a))for(unsigned c=0;c<4;++c){float v=f32(p+4+offsets[a]+c*4);if(!(v>=-16384&&v<=16384))return;varyings[a][c]=v;}
   for(unsigned sample=0;sample<samples_count;++sample){const unsigned char *q=p+4+sample_offsets[sample];float tx=filters[sample]?0:f32(q+16),ty=filters[sample]?0:f32(q+20);if(!(tx>=0&&tx<1&&ty>=0&&ty<1))return;
    samples[sample]=filters[sample]?texel(q,format[sample]):mix4(mix4(texel(q,format[sample]),texel(q+4,format[sample]),tx),mix4(texel(q+8,format[sample]),texel(q+12,format[sample]),tx),ty);
   }
   for(unsigned i=0;i<instructions;++i){const unsigned char *ins=data+256+i*32;unsigned op=u32(ins),dest=u32(ins+4),reg=dest&15,write=(dest>>4)&15;float4 result;
    if(op==3)result=samples[u32(ins+8)];else{
     float4 a=source_value(u32(ins+8),varyings,registers,immediates);result=a;
     if(op){float4 b=source_value(u32(ins+12),varyings,registers,immediates);result=op==1?a*b:a/b;}
    }
    for(unsigned c=0;c<4;++c)if(write&(1u<<c)){if(!finite(result[c]))return;registers[reg][c]=result[c];}
   }
   float4 color=registers[4];
   for(unsigned c=0;c<4;++c){if(!finite(color[c]))return;color[c]=clamp(color[c],1.0f);}
   float alpha=color.w;if(target==87)color=color.zyxw;unsigned packed=background;
   for(unsigned c=0;c<4;++c){unsigned logical=target==87&&c<3?2-c:c;if(!(mask&(1u<<logical)))continue;
    float result=color[c]*255.0f;if(blend)result=result+(float)((background>>(8*c))&255u)*(1.0f-alpha);
    packed=(packed&~(255u<<(c*8)))|(quantize(result)<<(c*8));
   }((unsigned*)(output+16))[n]=packed;
  }
  unsigned *header=(unsigned*)output;header[1]=1;header[2]=count;header[3]=hits;header[0]=0x4e52504f;return;
 }
 if(!vertices||vertices>2046||vertices%3||cull>2||ccw>1)return;
 const unsigned task_offset=2048+vertices*96;if(count>(262144u-task_offset)/12u)return;
 int clip[4];
 for(unsigned n=0;n<4;++n){clip[n]=(int)u32(data+32+n*4);if(clip[n]<-268435455||clip[n]>268435456)return;}
 if(clip[0]>clip[2]||clip[1]>clip[3])return;
 for(unsigned v=0;v<vertices;++v){const unsigned char *p=data+2048+v*96;
  if(!bounded((int)u32(p))||!bounded((int)u32(p+4))||!finite(f32(p+8))||!finite(f32(p+12))||!(f32(p+8)>=-1048576.0f&&f32(p+8)<=1048576.0f&&f32(p+12)>=-1048576.0f&&f32(p+12)<=1048576.0f)||!(f32(p+16)>0&&f32(p+16)<=65536)||u32(p+20)||u32(p+24)||u32(p+28))return;
  for(unsigned n=0;n<16;++n)if(!(f32(p+32+n*4)>=-16384&&f32(p+32+n*4)<=16384))return;
 }
 unsigned hits=0;
 #pragma clang loop unroll(disable)
 for(unsigned n=0;n<count;++n){const unsigned char *task=data+task_offset+n*12;unsigned base=u32(task);int px=(int)u32(task+4),py=(int)u32(task+8);
  if(base%3||base>vertices-3||!bounded(px)||!bounded(py))return;
  const unsigned char *v[3]={data+2048+base*96,data+2048+(base+1)*96,data+2048+(base+2)*96};int x[3],y[3];
  for(unsigned a=0;a<3;++a){x[a]=(int)u32(v[a]);y[a]=(int)u32(v[a]+4);}
  long long area=edge(x[0],y[0],x[1],y[1],x[2],y[2]);
  int covered=area!=0&&px>=clip[0]&&px<clip[2]&&py>=clip[1]&&py<clip[3];
  if(cull)covered=covered&&(((cull==2)!=ccw)?area>0:area<0);
  for(unsigned a=0;a<3;++a){unsigned b=(a+1)%3;int dx=x[b]-x[a],dy=y[b]-y[a];long long e=edge(x[a],y[a],x[b],y[b],px,py);
   int top=dy<0||(dy==0&&dx>0),reverse=dy>0||(dy==0&&dx<0);
   covered=covered&&(area>0?(e>0||(e==0&&top)):(e<0||(e==0&&reverse)));
  }
  if(!covered)continue;
  unsigned values[28]={0};unsigned *result=values;result[0]=1;++hits;
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
  for(unsigned sample=0;sample<samples_count;++sample){unsigned w=width[sample],h=height[sample];const unsigned char *desc=data+112+sample*16;
   unsigned slot=u32(desc),cx=u32(desc+4),cy=u32(desc+8),divide=u32(desc+12);float uvx=value(result[4+slot*4+cx]),uvy=value(result[4+slot*4+cy]);
   if(divide){float denominator=value(result[4+slot*4+divide-1]);uvx=uvx/denominator;uvy=uvy/denominator;}
   if(!finite(uvx)||!finite(uvy))return;
   float offset=filters[sample]?0.0f:0.5f;float x=clamp(clamp(uvx,1.0f)*(float)w-offset,(float)(w-1));float y=clamp(clamp(uvy,1.0f)*(float)h-offset,(float)(h-1));
   unsigned ix=(unsigned)x,iy=(unsigned)y,jx=!filters[sample]&&ix+1<w?ix+1:ix,jy=!filters[sample]&&iy+1<h?iy+1:iy;
   result[16+sample*4]=iy*w+ix;result[17+sample*4]=iy*w+jx;result[18+sample*4]=jy*w+ix;result[19+sample*4]=jy*w+jx;
   result[24+sample*2]=filters[sample]?0:bits(x-(float)ix);result[25+sample*2]=filters[sample]?0:bits(y-(float)iy);
  }
  unsigned *packed=(unsigned*)(output+16+n*stride);packed[0]=1;
  for(unsigned a=0;a<3;++a)if(live&(1u<<a))for(unsigned c=0;c<4;++c)packed[offsets[a]/4+c]=result[4+a*4+c];
  for(unsigned a=0;a<samples_count;++a){unsigned offset=sample_offsets[a]/4;for(unsigned tap=0;tap<(filters[a]?1u:4u);++tap)packed[offset+tap]=result[16+a*4+tap];if(!filters[a]){packed[offset+4]=result[24+a*2];packed[offset+5]=result[25+a*2];}}

 }
 unsigned *header=(unsigned*)output;header[1]=0;header[2]=count;header[3]=hits;header[0]=0x4e52504f;
}

__attribute__((noinline)) void *memset(void *destination,int value,__SIZE_TYPE__ count){
 volatile unsigned char *p=(volatile unsigned char*)destination;const unsigned char byte=(unsigned char)value;
 while(count&&((__UINTPTR_TYPE__)p&15)){*p++=byte;--count;}
 const uint4 fill=(uint4)((unsigned)byte*0x01010101u);
 while(count>=16){*(volatile uint4*)p=fill;p+=16;count-=16;}
 while(count){*p++=byte;--count;}
 return destination;
}
