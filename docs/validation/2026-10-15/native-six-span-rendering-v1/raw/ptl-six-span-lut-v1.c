/* Bounded experimental packed-RGBA rectangle renderer, NPU50XX.
 * Host MUST admit the pinned six-span Softmax carrier and source/output spans.
 * Input f16 storage transports raw bytes; no floating-point input conversion.
 * One invocation shades 4096 four-tap rectangle pixels. It does no display or driver management.
 */
#pragma STDC FP_CONTRACT OFF
#define INLINE static __attribute__((always_inline)) inline
INLINE unsigned u32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1]<<8) |
           ((unsigned)p[2]<<16) | ((unsigned)p[3]<<24);
}
INLINE float f32(const unsigned char *p) {
    union { unsigned u; float f; } value; value.u=u32(p); return value.f;
}
INLINE float clamp(float v,float lo,float hi) { return v<lo?lo:(v>hi?hi:v); }
INLINE unsigned rounded(float v) {
    v=clamp(v,0.0f,255.0f); unsigned base=(unsigned)v;
    float fraction=v-(float)base;
    return base+(fraction>0.5f || (fraction==0.5f && (base&1)));
}
INLINE float texel(const unsigned char *source,unsigned at,unsigned c) {
    return (float)source[at*4+c]/255.0f;
}
INLINE float mix(float a,float b,float t) { return a*(1.0f-t)+b*t; }
typedef float float4 __attribute__((ext_vector_type(4)));
typedef unsigned uint4 __attribute__((ext_vector_type(4)));
typedef int int4 __attribute__((ext_vector_type(4)));
typedef __fp16 half4 __attribute__((ext_vector_type(4)));
INLINE uint4 packed(const unsigned char *p) {
    unsigned word=u32(p);
    return ((uint4)word >> (uint4){0,8,16,24}) & (uint4)255;
}
static const float normalized_byte[256]={
0x0.0p+0f,0x1.0101020000000p-8f,0x1.0101020000000p-7f,0x1.8181820000000p-7f,0x1.0101020000000p-6f,0x1.4141420000000p-6f,0x1.8181820000000p-6f,0x1.c1c1c20000000p-6f,
0x1.0101020000000p-5f,0x1.2121220000000p-5f,0x1.4141420000000p-5f,0x1.6161620000000p-5f,0x1.8181820000000p-5f,0x1.a1a1a20000000p-5f,0x1.c1c1c20000000p-5f,0x1.e1e1e20000000p-5f,
0x1.0101020000000p-4f,0x1.1111120000000p-4f,0x1.2121220000000p-4f,0x1.3131320000000p-4f,0x1.4141420000000p-4f,0x1.5151520000000p-4f,0x1.6161620000000p-4f,0x1.7171720000000p-4f,
0x1.8181820000000p-4f,0x1.9191920000000p-4f,0x1.a1a1a20000000p-4f,0x1.b1b1b20000000p-4f,0x1.c1c1c20000000p-4f,0x1.d1d1d20000000p-4f,0x1.e1e1e20000000p-4f,0x1.f1f1f20000000p-4f,
0x1.0101020000000p-3f,0x1.09090a0000000p-3f,0x1.1111120000000p-3f,0x1.19191a0000000p-3f,0x1.2121220000000p-3f,0x1.29292a0000000p-3f,0x1.3131320000000p-3f,0x1.39393a0000000p-3f,
0x1.4141420000000p-3f,0x1.49494a0000000p-3f,0x1.5151520000000p-3f,0x1.59595a0000000p-3f,0x1.6161620000000p-3f,0x1.69696a0000000p-3f,0x1.7171720000000p-3f,0x1.79797a0000000p-3f,
0x1.8181820000000p-3f,0x1.89898a0000000p-3f,0x1.9191920000000p-3f,0x1.99999a0000000p-3f,0x1.a1a1a20000000p-3f,0x1.a9a9aa0000000p-3f,0x1.b1b1b20000000p-3f,0x1.b9b9ba0000000p-3f,
0x1.c1c1c20000000p-3f,0x1.c9c9ca0000000p-3f,0x1.d1d1d20000000p-3f,0x1.d9d9da0000000p-3f,0x1.e1e1e20000000p-3f,0x1.e9e9ea0000000p-3f,0x1.f1f1f20000000p-3f,0x1.f9f9fa0000000p-3f,
0x1.0101020000000p-2f,0x1.0505060000000p-2f,0x1.09090a0000000p-2f,0x1.0d0d0e0000000p-2f,0x1.1111120000000p-2f,0x1.1515160000000p-2f,0x1.19191a0000000p-2f,0x1.1d1d1e0000000p-2f,
0x1.2121220000000p-2f,0x1.2525260000000p-2f,0x1.29292a0000000p-2f,0x1.2d2d2e0000000p-2f,0x1.3131320000000p-2f,0x1.3535360000000p-2f,0x1.39393a0000000p-2f,0x1.3d3d3e0000000p-2f,
0x1.4141420000000p-2f,0x1.4545460000000p-2f,0x1.49494a0000000p-2f,0x1.4d4d4e0000000p-2f,0x1.5151520000000p-2f,0x1.5555560000000p-2f,0x1.59595a0000000p-2f,0x1.5d5d5e0000000p-2f,
0x1.6161620000000p-2f,0x1.6565660000000p-2f,0x1.69696a0000000p-2f,0x1.6d6d6e0000000p-2f,0x1.7171720000000p-2f,0x1.7575760000000p-2f,0x1.79797a0000000p-2f,0x1.7d7d7e0000000p-2f,
0x1.8181820000000p-2f,0x1.8585860000000p-2f,0x1.89898a0000000p-2f,0x1.8d8d8e0000000p-2f,0x1.9191920000000p-2f,0x1.9595960000000p-2f,0x1.99999a0000000p-2f,0x1.9d9d9e0000000p-2f,
0x1.a1a1a20000000p-2f,0x1.a5a5a60000000p-2f,0x1.a9a9aa0000000p-2f,0x1.adadae0000000p-2f,0x1.b1b1b20000000p-2f,0x1.b5b5b60000000p-2f,0x1.b9b9ba0000000p-2f,0x1.bdbdbe0000000p-2f,
0x1.c1c1c20000000p-2f,0x1.c5c5c60000000p-2f,0x1.c9c9ca0000000p-2f,0x1.cdcdce0000000p-2f,0x1.d1d1d20000000p-2f,0x1.d5d5d60000000p-2f,0x1.d9d9da0000000p-2f,0x1.ddddde0000000p-2f,
0x1.e1e1e20000000p-2f,0x1.e5e5e60000000p-2f,0x1.e9e9ea0000000p-2f,0x1.ededee0000000p-2f,0x1.f1f1f20000000p-2f,0x1.f5f5f60000000p-2f,0x1.f9f9fa0000000p-2f,0x1.fdfdfe0000000p-2f,
0x1.0101020000000p-1f,0x1.0303040000000p-1f,0x1.0505060000000p-1f,0x1.0707080000000p-1f,0x1.09090a0000000p-1f,0x1.0b0b0c0000000p-1f,0x1.0d0d0e0000000p-1f,0x1.0f0f100000000p-1f,
0x1.1111120000000p-1f,0x1.1313140000000p-1f,0x1.1515160000000p-1f,0x1.1717180000000p-1f,0x1.19191a0000000p-1f,0x1.1b1b1c0000000p-1f,0x1.1d1d1e0000000p-1f,0x1.1f1f200000000p-1f,
0x1.2121220000000p-1f,0x1.2323240000000p-1f,0x1.2525260000000p-1f,0x1.2727280000000p-1f,0x1.29292a0000000p-1f,0x1.2b2b2c0000000p-1f,0x1.2d2d2e0000000p-1f,0x1.2f2f300000000p-1f,
0x1.3131320000000p-1f,0x1.3333340000000p-1f,0x1.3535360000000p-1f,0x1.3737380000000p-1f,0x1.39393a0000000p-1f,0x1.3b3b3c0000000p-1f,0x1.3d3d3e0000000p-1f,0x1.3f3f400000000p-1f,
0x1.4141420000000p-1f,0x1.4343440000000p-1f,0x1.4545460000000p-1f,0x1.4747480000000p-1f,0x1.49494a0000000p-1f,0x1.4b4b4c0000000p-1f,0x1.4d4d4e0000000p-1f,0x1.4f4f500000000p-1f,
0x1.5151520000000p-1f,0x1.5353540000000p-1f,0x1.5555560000000p-1f,0x1.5757580000000p-1f,0x1.59595a0000000p-1f,0x1.5b5b5c0000000p-1f,0x1.5d5d5e0000000p-1f,0x1.5f5f600000000p-1f,
0x1.6161620000000p-1f,0x1.6363640000000p-1f,0x1.6565660000000p-1f,0x1.6767680000000p-1f,0x1.69696a0000000p-1f,0x1.6b6b6c0000000p-1f,0x1.6d6d6e0000000p-1f,0x1.6f6f700000000p-1f,
0x1.7171720000000p-1f,0x1.7373740000000p-1f,0x1.7575760000000p-1f,0x1.7777780000000p-1f,0x1.79797a0000000p-1f,0x1.7b7b7c0000000p-1f,0x1.7d7d7e0000000p-1f,0x1.7f7f800000000p-1f,
0x1.8181820000000p-1f,0x1.8383840000000p-1f,0x1.8585860000000p-1f,0x1.8787880000000p-1f,0x1.89898a0000000p-1f,0x1.8b8b8c0000000p-1f,0x1.8d8d8e0000000p-1f,0x1.8f8f900000000p-1f,
0x1.9191920000000p-1f,0x1.9393940000000p-1f,0x1.9595960000000p-1f,0x1.9797980000000p-1f,0x1.99999a0000000p-1f,0x1.9b9b9c0000000p-1f,0x1.9d9d9e0000000p-1f,0x1.9f9fa00000000p-1f,
0x1.a1a1a20000000p-1f,0x1.a3a3a40000000p-1f,0x1.a5a5a60000000p-1f,0x1.a7a7a80000000p-1f,0x1.a9a9aa0000000p-1f,0x1.ababac0000000p-1f,0x1.adadae0000000p-1f,0x1.afafb00000000p-1f,
0x1.b1b1b20000000p-1f,0x1.b3b3b40000000p-1f,0x1.b5b5b60000000p-1f,0x1.b7b7b80000000p-1f,0x1.b9b9ba0000000p-1f,0x1.bbbbbc0000000p-1f,0x1.bdbdbe0000000p-1f,0x1.bfbfc00000000p-1f,
0x1.c1c1c20000000p-1f,0x1.c3c3c40000000p-1f,0x1.c5c5c60000000p-1f,0x1.c7c7c80000000p-1f,0x1.c9c9ca0000000p-1f,0x1.cbcbcc0000000p-1f,0x1.cdcdce0000000p-1f,0x1.cfcfd00000000p-1f,
0x1.d1d1d20000000p-1f,0x1.d3d3d40000000p-1f,0x1.d5d5d60000000p-1f,0x1.d7d7d80000000p-1f,0x1.d9d9da0000000p-1f,0x1.dbdbdc0000000p-1f,0x1.ddddde0000000p-1f,0x1.dfdfe00000000p-1f,
0x1.e1e1e20000000p-1f,0x1.e3e3e40000000p-1f,0x1.e5e5e60000000p-1f,0x1.e7e7e80000000p-1f,0x1.e9e9ea0000000p-1f,0x1.ebebec0000000p-1f,0x1.ededee0000000p-1f,0x1.efeff00000000p-1f,
0x1.f1f1f20000000p-1f,0x1.f3f3f40000000p-1f,0x1.f5f5f60000000p-1f,0x1.f7f7f80000000p-1f,0x1.f9f9fa0000000p-1f,0x1.fbfbfc0000000p-1f,0x1.fdfdfe0000000p-1f,0x1.0000000000000p+0f
};
INLINE float4 texel4(const unsigned char *p,unsigned at) {
    uint4 value=packed(p+at*4); return (float4){normalized_byte[value.x],normalized_byte[value.y],normalized_byte[value.z],normalized_byte[value.w]};
}
INLINE float4 mix4(float4 a,float4 b,float t) { return a*(1.0f-t)+b*t; }

INLINE float edge(float ax,float ay,float bx,float by,float x,float y) {
    return (bx-ax)*(y-ay)-(by-ay)*(x-ax);
}
INLINE float4 coordinate(float x,float y,const float *s,const float *uv,unsigned triangle,unsigned barycentric) {
    float u,v;
    if(barycentric){
        float ax=triangle?s[2]:s[0],ay=triangle?s[3]:s[1];
        float bx=triangle?s[0]:s[2],by=triangle?s[3]:s[1];
        float cx=triangle?s[0]:s[2],cy=triangle?s[1]:s[3];
        float w0=edge(bx,by,cx,cy,x,y),w1=edge(cx,cy,ax,ay,x,y),w2=edge(ax,ay,bx,by,x,y);
        float den=(w0+w1)+w2;
        float ua=uv[triangle?2:0],ub=uv[triangle?0:2],uc=uv[triangle?0:2];
        float va=uv[triangle?3:1],vb=uv[triangle?3:1],vc=uv[triangle?1:3];
        u=((w0*ua+w1*ub)+w2*uc)/den;v=((w0*va+w1*vb)+w2*vc)/den;
    }else {u=uv[0]+((x-s[0])/(s[2]-s[0]))*(uv[2]-uv[0]);v=uv[1]+((y-s[1])/(s[3]-s[1]))*(uv[3]-uv[1]);}
    return (float4){u,v,0,0};
}
INLINE float4 sample4(const unsigned char *source,unsigned sw,unsigned sh,float u,float v,unsigned filter,const unsigned *crop,unsigned *valid) {
    float x=clamp(clamp(u,0,1)*(float)sw-(filter?.5f:0.0f),0,(float)(sw-1));
    float y=clamp(clamp(v,0,1)*(float)sh-(filter?.5f:0.0f),0,(float)(sh-1));
    unsigned ix=(unsigned)x,iy=(unsigned)y,jx=ix+1<sw?ix+1:ix,jy=iy+1<sh?iy+1:iy;
    float tx=x-(float)ix,ty=y-(float)iy;
    if(ix<crop[0] || iy<crop[1] || (filter?jx:ix)>=crop[0]+crop[2] || (filter?jy:iy)>=crop[1]+crop[3]){*valid=0;return (float4)0;}
    ix-=crop[0];jx-=crop[0];iy-=crop[1];jy-=crop[1];sw=crop[2];
    float4 value=texel4(source,iy*sw+ix);
    if(filter)value=mix4(mix4(value,texel4(source,iy*sw+jx),tx),mix4(texel4(source,jy*sw+ix),texel4(source,jy*sw+jx),tx),ty);
    return value;
}

void nr_probe_kernel(const unsigned char *params) {
    if(!params) return;
    for(unsigned n=0;n<2;++n){const unsigned char *p=params+n*40;
        if(!u32(p) || u32(p+4) || u32(p+8)!=1 || u32(p+12)!=4 || !u32(p+16) || !u32(p+20) || u32(p+24)!=2 || u32(p+28)!=0x4321 || u32(p+32) || u32(p+36)!=2)return;
        const unsigned *dims=(const unsigned*)u32(p+16);
        if(dims[0]!=131072 || dims[1]!=1 || dims[2]!=1 || dims[3]!=1)return;
    }
    const unsigned char *data=(const unsigned char *)u32(params);
    __fp16 *output=(__fp16 *)u32(params+40);
    unsigned count=4096,source_bytes=262144;
    unsigned sw=u32(data+8),sh=u32(data+12),w=u32(data+16),h=u32(data+20);
    unsigned first=u32(data+24),filter=u32(data+32),blend=u32(data+36);
    unsigned sf=u32(data+40),df=u32(data+44),opaque=u32(data+48),mask=u32(data+52);
    if(u32(data)!=0x4e525231 || u32(data+4)!=3 || !sw || sw>4096 || !sh || sh>4096 ||
       !w || w>4096 || !h || h>8192 || first>w*h || w*h-first<count ||
       u32(data+28)!=count || filter>1 || blend>1 ||
       (sf!=28 && sf!=87) || (df!=28 && df!=87) || opaque>1 || mask>15 ||
       u32(data+56)!=source_bytes || u32(data+60)) return;
    unsigned crop[4];for(unsigned c=0;c<4;++c)crop[c]=u32(data+160+c*4);
    if(!crop[2] || !crop[3] || crop[0]>sw || crop[1]>sh || crop[2]>sw-crop[0] || crop[3]>sh-crop[1] ||
       source_bytes<192+count*4 || crop[2]*crop[3]>(source_bytes-192-count*4)/4 ||
       u32(data+176) || u32(data+180) || u32(data+184) || u32(data+188)) return;
    float color[4],uv[4],screen[4]; int bounds[4],clip[4];
    for(unsigned c=0;c<4;++c) {
        color[c]=f32(data+64+c*4); uv[c]=f32(data+80+c*4); screen[c]=f32(data+128+c*4);
        if(!(screen[c]>=-4096 && screen[c]<=4096))return;
        bounds[c]=(int)u32(data+96+c*4);clip[c]=(int)u32(data+112+c*4);
        if(!(color[c]>=0 && color[c]<=1) || !(uv[c]>=-16 && uv[c]<=16) ||
           bounds[c]<-1048576 || bounds[c]>1048576 || clip[c]<0 || clip[c]>2097152) return;
    }
    if(bounds[0]>=bounds[2] || bounds[1]>=bounds[3] || clip[0]>clip[2] || clip[1]>clip[3]) return;
    if(screen[0]>=screen[2] || screen[1]>=screen[3] || u32(data+144)>1 || u32(data+148) || u32(data+152) || u32(data+156))return;
    unsigned barycentric=u32(data+144);
    const unsigned char *source=data+192,*background=data+192+crop[2]*crop[3]*4;
    float4 tint=(float4){color[0],color[1],color[2],color[3]};
    if(df==87)tint=tint.zyxw;
    uint4 *padding=(uint4*)output;for(unsigned i=0;i<16384;++i)padding[i]=(uint4)0;
    unsigned px=first%w,py=first/w,cached_x=0xffffffff,cached_y=0xffffffff,cached_triangle=0xffffffff;
    float4 q00=(float4)0,q10=(float4)0,q01=(float4)0,q11=(float4)0;
    #pragma clang loop unroll(disable)
    for(unsigned i=0;i<count;++i) {
        int sx=(int)px*256+128,sy=(int)py*256+128;
        int covered=sx>=bounds[0] && sx<bounds[2] && sy>=bounds[1] && sy<bounds[3] &&
                    sx>=clip[0] && sx<clip[2] && sy>=clip[1] && sy<clip[3];
        uint4 bg=packed(background+i*4);float4 value=(float4)0;unsigned valid=1;
        if(covered) {
            unsigned triangle=((float)(sx-bounds[0])*(float)(bounds[3]-bounds[1]) >= (float)(sy-bounds[1])*(float)(bounds[2]-bounds[0]))?0:1;
            unsigned qx=px&~1u,qy=py&~1u;
            if(qx!=cached_x || qy!=cached_y || triangle!=cached_triangle){
                q00=coordinate((float)qx+.5f,(float)qy+.5f,screen,uv,triangle,barycentric);
                q10=coordinate((float)qx+1.5f,(float)qy+.5f,screen,uv,triangle,barycentric);
                q01=coordinate((float)qx+.5f,(float)qy+1.5f,screen,uv,triangle,barycentric);
                q11=coordinate((float)qx+1.5f,(float)qy+1.5f,screen,uv,triangle,barycentric);
                cached_x=qx;cached_y=qy;cached_triangle=triangle;
            }
            float4 left=(py&1)?q01:q00,right=(py&1)?q11:q10;
            float4 top=(px&1)?q10:q00,bottom=(px&1)?q11:q01;
            float4 pos=(px&1)?right:left,dx=right-left,dy=bottom-top;
            float4 a=(dx*.375f+pos)+dy*(-.125f);
            float4 b=(dx*(-.125f)+pos)+dy*(-.375f);
            float4 c=(dx*(-.375f)+pos)+dy*.125f;
            float4 d=(dx*.125f+pos)+dy*.375f;
            float4 va=sample4(source,sw,sh,a.x,a.y,filter,crop,&valid),vb=sample4(source,sw,sh,b.x,b.y,filter,crop,&valid);
            float4 vc=sample4(source,sw,sh,c.x,c.y,filter,crop,&valid),vd=sample4(source,sw,sh,d.x,d.y,filter,crop,&valid);
            float4 sampled=(vd+((vb+va)+vc))*.25f;
            if(opaque)sampled.w=1.0f;
            float alpha=sampled.w*color[3];
            if(sf!=df)sampled=sampled.zyxw;
            value=sampled*tint*255.0f;
            if(blend)value=value+__builtin_convertvector(bg,float4)*(1.0f-alpha);
        }
        #pragma clang loop unroll(full)
        for(unsigned c=0;c<4;++c) {
            unsigned result=covered && (mask&(1u<<c))?rounded(value[c]):bg[c];
            output[i*4+c]=valid?(__fp16)(float)result:(__fp16)-1.0f;
        }
        ++px;if(px==w){px=0;++py;}
    }
}
