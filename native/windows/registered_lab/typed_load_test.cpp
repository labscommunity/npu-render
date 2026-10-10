#include "vertex_executor.hpp"
#include "shader_contract.hpp"
#include "shader_reference.hpp"
#include "typed_load_fixture.hpp"
#include "triangle_texture_draw.hpp"
#include "indexed_exchange_protocol.hpp"
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <limits>
using namespace nr::shader;
#define TEST(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
Program compile(const char *s,const char *profile){const auto w=nr::registered_lab::shader_tokens(s,profile);return parse_program(w.data(),w.size(),profile[0]=='v'?Stage::vertex:Stage::pixel);}
void integer_operations(const std::string &backend){
    const auto shifts=compile(R"(struct O{float4 p:SV_Position;int4 b:COLOR;};O main(int4 v:POSITION,uint s:INDEX){O o;o.p=float4(0,0,0,1);o.b=v>>s;return o;})","vs_4_0");
    std::vector<VertexRegisters> input(128);
    for(unsigned n=0;n<input.size();++n){input[n][0]={0x80000001u+n,0xffffffffu-n,0x12345678u+n*374763u,n*729473u};input[n][1][0]=n;}
    {std::printf("{\"stage\":\"ishr\"}\n");std::fflush(stdout);VertexExecutor executor(shifts,uint32_t(input.size()),backend);const auto result=executor.execute(input,{});
     TEST(result.execution_device.rfind(backend,0)==0);for(size_t n=0;n<input.size();++n)TEST(result.vertices[n]==reference_vertex(shifts,input[n],{}));}
    const auto mad=compile(R"(struct O{float4 p:SV_Position;int4 b:COLOR;};O main(int4 v:POSITION,int4 b:INDEX,int4 c:DATA){O o;o.p=float4(0,0,0,1);o.b=v*b-c;return o;})","vs_4_0");
    for(unsigned n=0;n<input.size();++n){input[n][1]={n*4317+7,0x87654321,0xffffffff,n};input[n][2]={3,n,0x80000000,0x7fffffff};}
    {std::printf("{\"stage\":\"imad\"}\n");std::fflush(stdout);VertexExecutor executor(mad,uint32_t(input.size()),backend);const auto result=executor.execute(input,{});
     for(size_t n=0;n<input.size();++n)TEST(result.vertices[n]==reference_vertex(mad,input[n],{}));}
    const auto conversion=compile(R"(struct O{float4 p:SV_Position;int4 b:COLOR;};O main(float4 v:POSITION){O o;o.p=float4(0,0,0,1);o.b=(int4)v;return o;})","vs_4_0");
    const float values[]{0,-0.f,.999f,-.999f,1.999f,-1.999f,2147483520.f,2147483648.f,-2147483648.f,-2147483904.f,
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),16777217.f,765.5f,-450.75f};
    for(size_t n=0;n<input.size();++n)for(unsigned c=0;c<4;++c)input[n][0][c]=as_bits(values[(n*4+c)%std::size(values)]);
    {std::printf("{\"stage\":\"ftoi\"}\n");std::fflush(stdout);VertexExecutor executor(conversion,uint32_t(input.size()),backend);const auto result=executor.execute(input,{});
     for(size_t n=0;n<input.size();++n)if(result.vertices[n]!=reference_vertex(conversion,input[n],{})){
        const auto expected=reference_vertex(conversion,input[n],{});
        for(unsigned r=0;r<16;++r)for(unsigned c=0;c<4;++c)if(result.vertices[n][r][c]!=expected[r][c])
            std::fprintf(stderr,"FTOI mismatch n=%zu r=%u c=%u actual=%08x expected=%08x\n",n,r,c,result.vertices[n][r][c],expected[r][c]);
        TEST(false);
     }}
    std::printf("{\"integer_ops\":[\"ishr\",\"imad\",\"ftoi\"],\"exact_components\":1536,\"execution_device\":\"%s\"}\n",backend.c_str());std::fflush(stdout);
}
void raster_lookup(const std::string &backend,const std::string &packet_path){
    using namespace nr::raster;
    const auto vs=compile(R"(struct O{float4 p:SV_Position;noperspective float2 uv:TEXCOORD;nointerpolation uint3 d:COLOR;};
O main(float4 p:POSITION,float2 uv:TEXCOORD,uint3 d:COLOR){O o;o.p=p;o.uv=uv;o.d=d;return o;})","vs_4_0");
    const auto &words=nr::registered_lab::test_fixture::typed_lookup_pixel_words;const auto ps=parse_pixel(words,std::size(words));
    PackedResources resources{};resources[0]={512,1,42,{}};resources[0].pixels.resize(512);
    for(unsigned n=0;n<512;++n){const uint32_t value=(n*37)%256;std::memcpy(resources[0].pixels[n].data(),&value,4);}
    resources[1]={256,1,87,{}};resources[1].pixels.resize(256);
    for(unsigned n=0;n<256;++n)resources[1].pixels[n]={uint8_t(n),uint8_t(255-n),uint8_t(n*17),255};
    const unsigned corners[6][2]{{0,0},{64,0},{64,64},{0,0},{64,64},{0,64}};
    std::vector<VertexRegisters> inputs(6);
    for(unsigned n=0;n<6;++n){const auto x=float(corners[n][0]),y=float(corners[n][1]);
        inputs[n][0]={as_bits(x/32-1),as_bits(1-y/32),as_bits(.5f),as_bits(1)};
        inputs[n][1]={as_bits(x),as_bits(y),0,0};inputs[n][2]={0,8,0,1};}
    const Viewport viewport{0,0,64,64,0,1};std::vector<PackedColor> background(4096,{7,11,13,17});
    const std::array<int32_t,4> clip{3,5,61,59};
    TriangleTextureDrawExecutor executor(vs,ps,6,64,64,87,resources,{},backend,false,15,CullMode::back,false,4096);
    const auto result=executor.execute(inputs,{}, {},viewport,resources,background,clip);
    TEST(result.covered_samples==58*54 && result.execution_device.rfind(backend,0)==0);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const auto n=y*64+x;
        const auto expected=x>=3 && x<61 && y>=5 && y<59?resources[1].pixels[((y*8+x/8)*37)%256]:background[n];
        if(result.pixels[n]!=expected){std::fprintf(stderr,"lookup raster mismatch x=%u y=%u\n",x,y);TEST(false);}}
    if(!packet_path.empty()){
        namespace ex=nr::registered_lab::indexed_exchange;
        ex::Request request;request.header.abi=8;request.header.nonce[0]=0x76543210;request.header.nonce[1]=0xcabc0123;
        request.header.width=request.header.height=64;request.header.vertices=6;request.header.format=87;request.header.flags=0x2f46;request.header.sequence=1;request.header.client_pid=42;
        request.vertex=vs;request.pixel=ps;request.inputs=inputs;request.viewport=viewport;request.scissor=clip;request.resources=resources;request.background=background;
        const auto bytes=ex::encode(request);const auto decoded=ex::decode(bytes);TEST(decoded.inputs==inputs && resource_layout_equal(decoded.resources,resources));
        std::ifstream prior(packet_path,std::ios::binary),prior_pixels(packet_path+".pixels",std::ios::binary);TEST(!prior && !prior_pixels);prior.close();prior_pixels.close();
        std::ofstream packet(packet_path,std::ios::binary),pixels(packet_path+".pixels",std::ios::binary);TEST(bool(packet) && bool(pixels));
        packet.write(reinterpret_cast<const char *>(bytes.data()),std::streamsize(bytes.size()));
        pixels.write(reinterpret_cast<const char *>(result.pixels.data()),std::streamsize(result.pixels.size()*4));
        packet.close();pixels.close();TEST(bool(packet) && bool(pixels));
    }
    std::printf("{\"typed_lookup_triangle_draw\":true,\"covered_samples\":3132,\"exact_pixels\":4096,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",backend.c_str());std::fflush(stdout);
}
int main(int argc,char **argv)try{
    TEST(argc==1 || argc==2 || (argc==4 && !std::strcmp(argv[2],"--emit-packet")));const std::string backend=argc>=2?argv[1]:"CPU";integer_operations(backend);
    const auto &words=nr::registered_lab::test_fixture::typed_lookup_pixel_words;const auto p=parse_pixel(words,std::size(words));
    TEST(p.texture_dimensions[0]==1 && p.texture_uint[0] && p.texture_dimensions[1]==3 && !p.texture_uint[1]);
    TextureShapes shapes{};shapes[0]={128,1,1};shapes[1]={256,4,15};TexturePixels textures;
    textures[0].resize(128);textures[1].resize(1024);
    for(unsigned n=0;n<128;++n)textures[0][n][0]=as_float((n*71+17)%256);
    for(unsigned n=0;n<1024;++n)for(unsigned c=0;c<4;++c)textures[1][n][c]=float((n*31+c*53)%256)/255;
    std::vector<VertexRegisters> input(128);
    for(unsigned n=0;n<input.size();++n){input[n][1]={as_bits(float(int(n)-24)+.75f),as_bits(float(n%10)-2.25f),0,0};input[n][2]={n%3,16,n%6,0};}
    std::printf("{\"stage\":\"captured_shell\"}\n");std::fflush(stdout);
    uint64_t checked=0;
    {PixelExecutor executor(p,uint32_t(input.size()),backend,shapes);const auto result=executor.execute(input,{},textures);TEST(result.execution_device.rfind(backend,0)==0);
     for(size_t n=0;n<input.size();++n){TEST(result.vertices[n]==reference_vertex(p,input[n],{},shapes,{},textures));checked+=4;}}
    // Missing components, UINT bit patterns, negative/OOB addresses and an
    // unbound resource are independent of sampler CLAMP behavior.
    const auto load=compile(R"(Buffer<uint4> t:register(t0);uint4 main(nointerpolation uint a:INDEX):SV_Target{return t.Load(a);})","ps_4_0");
    for(const unsigned mask:{1u,3u,15u}){
        TextureShapes shape{};shape[0]={4,1,mask};TexturePixels data;data[0].resize(4);
        const uint32_t patterns[4]{0xffffffffu,0x80000001u,0x12345678u,0x7fc00001u};
        for(unsigned n=0;n<4;++n)for(unsigned c=0;c<4;++c)data[0][n][c]=as_float(patterns[(n+c)%4]);
        for(unsigned n=0;n<input.size();++n)input[n][0][0]=n%7==6?UINT32_MAX:n%7;
        PixelExecutor executor(load,uint32_t(input.size()),backend,shape);const auto result=executor.execute(input,{},data);
        for(size_t n=0;n<input.size();++n){TEST(result.vertices[n]==reference_vertex(load,input[n],{},shape,{},data));checked+=4;}
    }
    {PixelExecutor executor(load,uint32_t(input.size()),backend);const auto result=executor.execute(input,{});
     for(const auto &v:result.vertices){TEST(v[0]==VertexBits{});checked+=4;}}
    raster_lookup(backend,argc==4?argv[3]:"");
    std::printf("{\"captured_shell_shader\":true,\"typed_load_components\":%llu,\"exact_bits_verified\":true,\"out_of_bounds_and_missing_components\":true,\"execution_device\":\"%s\",\"desktop_offload_tested\":false}\n",static_cast<unsigned long long>(checked),backend.c_str());return 0;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
