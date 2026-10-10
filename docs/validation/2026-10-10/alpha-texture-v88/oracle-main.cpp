#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void check(HRESULT h){if(FAILED(h)){std::fprintf(stderr,"HRESULT=%08lx\n",static_cast<unsigned long>(h));throw std::runtime_error("D3D call failed");}}
int main()try{
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL level{};
 check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c));
 std::array<unsigned char,256> alpha{};for(unsigned i=0;i<256;++i)alpha[i]=static_cast<unsigned char>(i);
 D3D11_TEXTURE2D_DESC td{};td.Width=256;td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
 td.Format=DXGI_FORMAT_A8_UNORM;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA data{};data.pSysMem=alpha.data();data.SysMemPitch=256;
 ComPtr<ID3D11Texture2D>t;check(d->CreateTexture2D(&td,&data,&t));
 ComPtr<ID3D11ShaderResourceView>srv;check(d->CreateShaderResourceView(t.Get(),nullptr,&srv));
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=256*16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
 bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=16;
 ComPtr<ID3D11Buffer>out,stage;check(d->CreateBuffer(&bd,nullptr,&out));
 ComPtr<ID3D11UnorderedAccessView>uav;check(d->CreateUnorderedAccessView(out.Get(),nullptr,&uav));
 bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 check(d->CreateBuffer(&bd,nullptr,&stage));
 const char *hlsl="Texture2D<float4> t;RWStructuredBuffer<float4> o;[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){o[id.x]=t.Load(int3(id.x,0,0));}";
 ComPtr<ID3DBlob>code,err;check(D3DCompile(hlsl,strlen(hlsl),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,&err));
 ComPtr<ID3D11ComputeShader>cs;check(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs));
 auto view=srv.Get();auto dest=uav.Get();c->CSSetShader(cs.Get(),nullptr,0);c->CSSetShaderResources(0,1,&view);c->CSSetUnorderedAccessViews(0,1,&dest,nullptr);c->Dispatch(4,1,1);
 dest=nullptr;c->CSSetUnorderedAccessViews(0,1,&dest,nullptr);c->CopyResource(stage.Get(),out.Get());
 D3D11_MAPPED_SUBRESOURCE mapped{};check(c->Map(stage.Get(),0,D3D11_MAP_READ,0,&mapped));
 const auto values=static_cast<const float *>(mapped.pData);bool passed=true;double maximum=0;
 for(unsigned i=0;i<256;++i){for(unsigned k=0;k<3;++k)passed=passed&&values[i*4+k]==0;maximum=(std::max)(maximum,std::abs(double(values[i*4+3])-double(i)/255));}
 c->Unmap(stage.Get(),0);passed=passed&&maximum<1e-7;
 std::printf("{\"backend\":\"WARP\",\"alpha_encodings\":256,\"rgb_zero\":%s,\"maximum_alpha_error\":%.10g,\"passed\":%s}\n",passed?"true":"false",maximum,passed?"true":"false");return passed?0:1;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}
