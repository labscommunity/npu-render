// Independent scalar IR oracle for the packed NPU coordinate/color experiment.
#include "shader_reference.hpp"
#include "captured.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace nr::shader;
uint32_t state;
float random_value(){state=1664525*state+1013904223;return float(state>>8)/16777216.0f;}
void put(std::vector<unsigned char>&v,size_t at,uint32_t x){std::memcpy(v.data()+at,&x,4);}
int main(int argc,char**argv){
 if(argc!=4)return 2;unsigned fixture=unsigned(std::stoul(argv[1]));state=0x15a8192u+fixture;
 auto program=parse_program(pixel,std::size(pixel),Stage::pixel);program.constants[1]=6;
 std::vector<Program> prefixes;unsigned sample=0;
 for(size_t n=0;n<program.instructions.size();++n){auto &instruction=program.instructions[n];if(instruction.opcode!=Opcode::sample)continue;
  auto prefix=program;prefix.instructions.resize(n);Instruction read{};read.opcode=Opcode::move;read.source_count=1;read.sources[0]=instruction.sources[0];read.destination.kind=Register::output;read.destination.mask=3;prefix.instructions.push_back(read);prefixes.push_back(prefix);
  instruction.opcode=Opcode::move;instruction.source_count=1;instruction.sources[0]={};instruction.sources[0].kind=Register::constant;instruction.sources[0].index=1;instruction.sources[0].element=sample++;
 }
 std::vector<unsigned char> data(262144),expected(262144);const unsigned counts[]={0,1,17,128,257,1024};
 unsigned phase=fixture%2,count=counts[(fixture/2)%6];
 put(data,0,0x4e525433);put(data,4,1);put(data,8,phase);put(data,12,count);put(data,16,262144);
 Constants constants{};constants[0].resize(6);constants[1].resize(6);
 for(unsigned n=0;n<6;++n)for(unsigned c=0;c<4;++c)constants[0][n][c]=as_bits(.03125f+random_value()*1.96875f);
 if(fixture%3==0)for(unsigned c=0;c<4;++c){constants[0][2][c]=as_bits(c<2?0.0f:1.0f);constants[0][4][c]=as_bits(c<2?0.0f:1.0f);}
 std::memcpy(data.data()+64,constants[0].data(),96);
 unsigned widths[3]={64,672,704},heights[3]={64,1024,704},formats[3]={87,87,28};
 if(fixture%4==0){widths[0]=1;heights[1]=1;widths[2]=19;heights[2]=7;}
 if(fixture%5==0)for(auto &f:formats)f=f==87?28:87;
 for(unsigned n=0;n<3;++n){put(data,160+n*12,widths[n]);put(data,164+n*12,heights[n]);put(data,168+n*12,formats[n]);}
 for(unsigned pixel=0;pixel<count;++pixel){
  Registers inputs{};for(unsigned n=0;n<5;++n)for(unsigned c=0;c<4;++c)inputs[n][c]=as_bits(.03125f+random_value()*1.96875f);
  if(fixture%7==0)for(unsigned n=0;n<5;++n)for(unsigned c=0;c<2;++c)inputs[n][c]=as_bits(-as_float(inputs[n][c]));
  size_t at=256+pixel*224;std::memcpy(data.data()+at,inputs.data(),80);
  if(!phase){
   for(unsigned n=0;n<6;++n){auto coords=reference_vertex(prefixes[n],inputs,constants)[0];unsigned slot=n==0?0:n==5?2:1,w=widths[slot],h=heights[slot];
    float u=as_float(coords[0]),v=as_float(coords[1]);if(!std::isfinite(u)||!std::isfinite(v))return 3;
    float x=std::clamp(std::clamp(u,0.0f,1.0f)*float(w)-.5f,0.0f,float(w-1));float y=std::clamp(std::clamp(v,0.0f,1.0f)*float(h)-.5f,0.0f,float(h-1));
    unsigned ix=unsigned(std::floor(x)),iy=unsigned(std::floor(y)),jx=std::min(ix+1,w-1),jy=std::min(iy+1,h-1);
    size_t result=16+pixel*144;put(expected,result+n*16,iy*w+ix);put(expected,result+n*16+4,iy*w+jx);put(expected,result+n*16+8,jy*w+ix);put(expected,result+n*16+12,jy*w+jx);put(expected,result+96+n*8,as_bits(x-float(ix)));put(expected,result+100+n*8,as_bits(y-float(iy)));
   }
  }else{
   for(unsigned n=0;n<6;++n){unsigned slot=n==0?0:n==5?2:1;float tx=random_value(),ty=random_value();if(pixel%9==0)tx=0;if(pixel%11==0)ty=.5f;put(data,at+80+n*8,as_bits(tx));put(data,at+84+n*8,as_bits(ty));float taps[4][4]{};
    for(unsigned tap=0;tap<4;++tap)for(unsigned c=0;c<4;++c){auto byte=static_cast<unsigned char>(unsigned(random_value()*256.0f));data[at+128+n*16+tap*4+c]=byte;unsigned logical=formats[slot]==87&&c<3?2-c:c;taps[tap][logical]=float(byte)/255.0f;}
    for(unsigned c=0;c<4;++c){volatile float a=taps[0][c]*(1.0f-tx)+taps[1][c]*tx;volatile float b=taps[2][c]*(1.0f-tx)+taps[3][c]*tx;constants[1][n][c]=as_bits(a*(1.0f-ty)+b*ty);}
   }
   auto color=reference_vertex(program,inputs,constants)[0];std::memcpy(expected.data()+16+pixel*16,color.data(),16);
  }
 }
 put(expected,0,0x4e52544f);put(expected,4,phase);put(expected,8,count);
 if(fixture>=96){switch((fixture-96)%12){
  case 0:put(data,0,0);break;case 1:put(data,4,2);break;case 2:put(data,8,2);break;case 3:put(data,12,1025);break;
  case 4:put(data,16,0);break;case 5:put(data,20,1);break;case 6:put(data,160,0);break;case 7:put(data,164,8193);break;
  case 8:put(data,168,0);break;case 9:put(data,64,0x7fc00000);break;case 10:put(data,24,1);break;case 11:put(data,28,1);break;
 }std::fill(expected.begin(),expected.end(),0);}
 for(unsigned n=0;n<2;++n){std::ofstream f(argv[2+n],std::ios::binary);const auto &v=n?expected:data;f.write(reinterpret_cast<const char*>(v.data()),v.size());if(!f)return 4;}
 return 0;
}
