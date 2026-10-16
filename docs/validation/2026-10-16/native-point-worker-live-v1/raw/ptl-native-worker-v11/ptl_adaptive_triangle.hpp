#pragma once
#include "triangle_texture_draw.hpp"
#include "ptl_geometry_triangle_draw.hpp"
#include "ptl_masked.hpp"
#include "ptl_masked_triangle_draw.hpp"
#include "vertex_executor.hpp"
#include <numeric>
#include <cstdio>
namespace nr::raster {
inline bool geometry_supported(const shader::Program &pixel,unsigned capacity,unsigned batch){
 if(!capacity||capacity>2046||capacity%3||!batch||batch>shader::RegisterExecutor::preferred_raster_batch(pixel)||shader::quad_derivatives(pixel))return false;
 const auto maximum=shader::RegisterExecutor::maximum_batch(pixel);
 const auto effective=batch<=maximum?batch:std::gcd(batch,maximum);
 if(!effective||effective>18432)return false;
 unsigned attributes=0;
 for(unsigned r=0;r<16;++r)if(pixel.inputs[r]&&r!=pixel.position_input){
  if(++attributes>4||(pixel.interpolation[r]!=1&&pixel.interpolation[r]!=2&&pixel.interpolation[r]!=4))return false;
 }
 return true;
}
class PtlAdaptiveTriangle {
 std::unique_ptr<PtlMaskedTriangleDrawExecutor> masked;
 std::unique_ptr<PtlGeometryTriangleDrawExecutor> geometry;
 std::unique_ptr<TriangleTextureDrawExecutor> original;
public:
 PtlAdaptiveTriangle(const shader::Program &v,const shader::Program &p,uint32_t n,uint32_t w,uint32_t h,uint32_t f,const PackedResources &resources,const shader::TextureSamplers &samplers,const std::string &backend,BlendMode blend,uint8_t mask,CullMode cull,bool ccw,uint32_t batch,bool enabled,bool masked_enabled){
  if(masked_enabled&&geometry_supported(p,n,batch)&&unsigned(blend)<=1&&PtlMasked::eligible(p,resources,samplers))masked=std::make_unique<PtlMaskedTriangleDrawExecutor>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch);
  else if(enabled&&geometry_supported(p,n,batch))geometry=std::make_unique<PtlGeometryTriangleDrawExecutor>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch);
  else original=std::make_unique<TriangleTextureDrawExecutor>(v,p,n,w,h,f,resources,samplers,backend,blend,mask,cull,ccw,batch);
 }
 UniformDrawExecutor::Result execute(const std::vector<shader::VertexRegisters> &inputs,const shader::VertexConstants &vc,const shader::VertexConstants &pc,const Viewport &viewport,const PackedResources &resources,const std::vector<PackedColor> &background,const std::optional<std::array<int32_t,4>> &scissor,const Color &blend_constant){
  if(masked){auto result=masked->execute(inputs,vc,pc,viewport,resources,background,scissor,blend_constant);
   std::printf("{\"ptl_masked_draw\":true,\"accepted\":true,\"provider_requests\":%llu,\"fallback_backend\":\"none\"}\n",static_cast<unsigned long long>(result.provider_requests));std::fflush(stdout);return result;
  }
  auto result=geometry?geometry->execute(inputs,vc,pc,viewport,resources,background,scissor,blend_constant):original->execute(inputs,vc,pc,viewport,resources,background,scissor,blend_constant);
  std::printf("{\"ptl_geometry_draw\":true,\"accepted\":%s,\"provider_requests\":%llu,\"fallback_backend\":\"%s\"}\n",geometry?"true":"false",static_cast<unsigned long long>(result.provider_requests),geometry?"none":"NPU");std::fflush(stdout);return result;
 }
};
}
