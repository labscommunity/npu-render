#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void check(HRESULT h){if(FAILED(h)){std::fprintf(stderr,"HRESULT=%08lx\n",static_cast<unsigned long>(h));throw std::runtime_error("D3D call failed");}}
int main()try{
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL level{};
 check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c));
 const char *hlsl="struct O {float4 p:SV_Position;uint id:ID;}; O main(uint id:SV_VertexID){O o;o.p=float4(0,0,0,1);o.id=id;return o;}";
 ComPtr<ID3DBlob>code,err;check(D3DCompile(hlsl,strlen(hlsl),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,&err));
 ComPtr<ID3D11VertexShader>vs;check(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs));
 D3D11_SO_DECLARATION_ENTRY entry{0,"ID",0,0,1,0};UINT stride=4;
 ComPtr<ID3D11GeometryShader>gs;check(d->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(),code->GetBufferSize(),&entry,1,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs));
 D3D11_BUFFER_DESC desc{};desc.ByteWidth=12;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
 ComPtr<ID3D11Buffer>output,staging,index;check(d->CreateBuffer(&desc,nullptr,&output));
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(d->CreateBuffer(&desc,nullptr,&staging));
 const UINT indices[3]{2,0,1};D3D11_SUBRESOURCE_DATA data{};data.pSysMem=indices;
 desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_INDEX_BUFFER;desc.CPUAccessFlags=0;check(d->CreateBuffer(&desc,&data,&index));
 c->VSSetShader(vs.Get(),nullptr,0);c->GSSetShader(gs.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);c->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R32_UINT,0);
 for(UINT indexed=0;indexed<2;++indexed){UINT offset=0;auto target=output.Get();c->SOSetTargets(1,&target,&offset);
  if(indexed)c->DrawIndexed(3,0,5);else c->Draw(3,7);
  c->SOSetTargets(0,nullptr,nullptr);c->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
  auto values=static_cast<const UINT *>(mapped.pData);std::printf("{\"backend\":\"WARP\",\"indexed\":%s,\"offset\":%u,\"ids\":[%u,%u,%u]}\n",indexed?"true":"false",indexed?5:7,values[0],values[1],values[2]);c->Unmap(staging.Get(),0);
 }
 return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
