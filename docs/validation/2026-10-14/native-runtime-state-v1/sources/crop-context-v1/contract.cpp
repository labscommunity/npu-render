#include "linear_texture_draw.hpp"
#include "opaque_affine_crop.hpp"
#include <cstdio>
#include <limits>
using namespace nr::raster;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
int main(int argc,char **argv)try{
 CHECK(argc==2 || argc==4);const std::string backend=argv[1];uint64_t checked=0,calls=0;unsigned rejected=0;double maximum=0;uint32_t random=42;
 auto byte=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return uint8_t(random);};
 for(const auto shape:{std::array<uint32_t,2>{64,48},{1024,768},{2880,1800}})for(const bool swap:{false,true}){
  const auto w=shape[0],h=shape[1],sw=w+32,sh=h+24;
  if(argc==4 && w!=uint32_t(std::stoul(argv[2])))continue;OpaqueAffineCrop executor(sw,sh,w,h,swap,backend);
  PackedTexture source{sw,sh,87,std::vector<PackedColor>(size_t(sw)*sh)};std::vector<PackedColor> bg(size_t(w)*h);
  for(const unsigned style:{0u,1u,2u}){
   if(argc==4 && style!=uint32_t(std::stoul(argv[3])))continue;
   const int32_t inset=style==0?0:128;const std::array<int32_t,4> bounds{inset,inset,int32_t(w*256)-inset,int32_t(h*256)-inset};
   const std::array<int32_t,4> clip=style==2?std::array<int32_t,4>{2,3,int32_t(w)-2,int32_t(h)-3}:std::array<int32_t,4>{0,0,int32_t(w),int32_t(h)};
   const std::array<float,4> uv{float((2+double(bounds[0])/256)/sw),float((1+double(bounds[1])/256)/sh),float((2+double(bounds[2])/256)/sw),float((1+double(bounds[3])/256)/sh)};const std::array<float,4> color{1,1,1,1};
   if(w==64 && !swap && style==0){
    const auto before=nr::provider::compile_statistics().calls;
    for(unsigned bad=0;bad<7;++bad){auto u=uv,c=color;auto b=bounds,sc=clip;bool point=false;
     if(bad==0)point=true;if(bad==1)c[0]=.999f;if(bad==2){u[0]+=.25f/sw;u[2]+=.25f/sw;}if(bad==3)u[2]+=.01f;if(bad==4)std::swap(u[0],u[2]);if(bad==5)u[0]=std::numeric_limits<float>::quiet_NaN();if(bad==6)sc[2]=sc[0];
     CHECK(!executor.execute(b,sc,u,c,source,bg,point));++rejected;
    }
    CHECK(nr::provider::compile_statistics().calls==before);
   }
   for(unsigned frame=0;frame<2;++frame){
    for(uint32_t y=0;y<sh;++y)for(uint32_t x=0;x<sw;++x)for(unsigned c=0;c<4;++c)source.pixels[size_t(y)*sw+x][c]=frame?byte():uint8_t(((x+y+c)&1)?255:0);
    for(auto &pixel:bg)for(auto &channel:pixel)channel=byte();
    std::printf("{\"crop_begin\":true,\"width\":%u,\"style\":%u,\"swap\":%s,\"frame\":%u}\n",w,style,swap?"true":"false",frame);std::fflush(stdout);
    const auto result=executor.execute(bounds,clip,uv,color,source,bg,false);CHECK(result && result->device.rfind(backend,0)==0 && result->maximum_byte_error<.5);++calls;
    uint64_t covered=0;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto at=size_t(y)*w+x;
     const bool inside=int32_t(x*256+128)>=bounds[0] && int32_t(x*256+128)<bounds[2] && int32_t(y*256+128)>=bounds[1] && int32_t(y*256+128)<bounds[3] && int32_t(x)>=clip[0] && int32_t(x)<clip[2] && int32_t(y)>=clip[1] && int32_t(y)<clip[3];
     if(!inside){CHECK(result->pixels[at]==bg[at]);checked+=4;continue;}++covered;
     const double tx=(double(uv[0])+(x+.5-double(bounds[0])/256)/(double(bounds[2]-bounds[0])/256)*(double(uv[2])-uv[0]))*sw-.5;
     const double ty=(double(uv[1])+(y+.5-double(bounds[1])/256)/(double(bounds[3]-bounds[1])/256)*(double(uv[3])-uv[1]))*sh-.5;
     const auto ix=int32_t(std::floor(tx)),iy=int32_t(std::floor(ty));const auto fx=tx-ix,fy=ty-iy;
     for(unsigned c=0;c<4;++c){double value=c==3?255.:0.;if(c!=3)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto sx=std::clamp(ix+dx,0,int32_t(sw)-1),sy=std::clamp(iy+dy,0,int32_t(sh)-1);value+=source.pixels[size_t(sy)*sw+sx][swap?2-c:c]*(dx?fx:1-fx)*(dy?fy:1-fy);}
      const auto actual=result->pixels[at][c];maximum=(std::max)(maximum,std::abs(actual-value));CHECK(actual==uint8_t(std::nearbyint(value)));++checked;
     }
    }
    CHECK(result->covered==covered);std::printf("{\"crop_case\":true,\"width\":%u,\"height\":%u,\"swap\":%s,\"style\":%u,\"frame\":%u,\"error_bound\":%.9g,\"execution_device\":\"%s\"}\n",w,h,swap?"true":"false",style,frame,result->maximum_byte_error,result->device.c_str());std::fflush(stdout);
   }
  }
 }
 std::printf("{\"passed\":true,\"checked_components\":%llu,\"npu_or_cpu_crop_calls\":%llu,\"negative_cases\":%u,\"maximum_oracle_error\":%.9g,\"execution_device\":\"%s\",\"desktop_offloaded\":false}\n",static_cast<unsigned long long>(checked),static_cast<unsigned long long>(calls),rejected,maximum,backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
