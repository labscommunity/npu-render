#pragma once
#include "shader_ir.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nr::shader {
// Structural lowering only. All per-pixel evaluation stays in the NPU kernel.
struct PtlProgram {
 struct Sample {unsigned texture=0,sampler=0,slot=0,x=0,y=0,divide=0;};
 std::array<unsigned,3> inputs{16,16,16},modes{};
 std::vector<Sample> samples;
 std::vector<std::array<uint32_t,4>> immediates;
 std::vector<std::array<uint32_t,8>> instructions;
 static void require(bool v){if(!v)throw std::invalid_argument("unsupported bounded pixel program");}
 static PtlProgram lower(const Program &p){
  require(p.stage==Stage::pixel&&!p.instructions.empty()&&p.instructions.size()<=8&&p.outputs[0]==15);
  for(unsigned i=1;i<16;++i)require(!p.outputs[i]);
  PtlProgram result;std::array<unsigned,16> input_map,temp_map;input_map.fill(16);temp_map.fill(16);unsigned input_count=0,temp_count=0;std::array<unsigned,5> initialized{};
  struct Coordinate {bool valid=false;unsigned input=0,lane=0,divide=0;};
  std::array<std::array<Coordinate,4>,16> coordinates{};
  const auto input=[&](unsigned index){require(index<16&&index!=p.position_input&&p.inputs[index]);const auto mode=p.interpolation[index];require(mode==1||mode==2||mode==4);
   if(input_map[index]==16){require(input_count<3);input_map[index]=input_count;result.inputs[input_count]=index;result.modes[input_count++]=mode;}return input_map[index];};
  const auto operand_ok=[](const Operand &o){require(o.modifier==Modifier::none&&!o.relative.enabled&&!o.element);for(auto lane:o.lanes)require(lane<4);};
  const auto encode=[&](const Operand &o,unsigned write){operand_ok(o);unsigned kind=0,index=0;
   if(o.kind==Register::input){index=input(o.index);for(unsigned c=0;c<4;++c)if(write&(1u<<c))require((p.inputs[o.index]&(1u<<o.lanes[c]))!=0);}
   else if(o.kind==Register::temporary){require(o.index<16&&temp_map[o.index]<4);kind=1;index=temp_map[o.index];for(unsigned c=0;c<4;++c)if(write&(1u<<c))require((initialized[index]&(1u<<o.lanes[c]))!=0);}
   else if(o.kind==Register::immediate){kind=2;for(auto word:o.literal){float value;std::memcpy(&value,&word,4);require(std::isfinite(value)&&value>=-16384&&value<=16384);}
    auto found=std::find(result.immediates.begin(),result.immediates.end(),o.literal);if(found==result.immediates.end()){require(result.immediates.size()<4);result.immediates.push_back(o.literal);index=unsigned(result.immediates.size()-1);}else index=unsigned(found-result.immediates.begin());}
   else require(false);
   unsigned code=kind|(index<<4);for(unsigned c=0;c<4;++c)code|=o.lanes[c]<<(8+c*2);return code;
  };
  for(const auto &i:p.instructions){require(!i.saturate);const auto &d=i.destination;operand_ok(d);require(d.mask&&d.mask<=15);
   unsigned destination=4;
   if(d.kind==Register::temporary){require(d.index<16&&d.index<p.temporaries);if(temp_map[d.index]==16){require(temp_count<4);temp_map[d.index]=temp_count++;}destination=temp_map[d.index];}
   else require(d.kind==Register::output&&d.index==0);
   const auto before=coordinates;const auto coordinate=[&](const Operand &o,unsigned channel){operand_ok(o);Coordinate value{};
    if(o.kind==Register::input){(void)input(o.index);require((p.inputs[o.index]&(1u<<o.lanes[channel]))!=0);value={true,o.index,o.lanes[channel],0};}
    else if(o.kind==Register::temporary){require(o.index<16);value=before[o.index][o.lanes[channel]];}return value;
   };
   std::array<uint32_t,8> instruction{};instruction[1]=destination|(d.mask<<4);std::array<Coordinate,4> next{};
   if(i.opcode==Opcode::sample){require(i.source_count==3&&result.samples.size()<2);const auto &texture=i.sources[1],&sampler=i.sources[2];operand_ok(texture);operand_ok(sampler);
    require(texture.kind==Register::texture&&texture.index<16&&p.textures[texture.index]&&!p.texture_uint[texture.index]&&p.texture_dimensions[texture.index]==3&&texture.lanes==std::array<uint32_t,4>{0,1,2,3});require(sampler.kind==Register::sampler&&sampler.index<16&&p.samplers[sampler.index]);
    const auto x=coordinate(i.sources[0],0),y=coordinate(i.sources[0],1);require(x.valid&&y.valid&&x.input==y.input&&x.divide==y.divide);
    instruction[0]=3;instruction[2]=unsigned(result.samples.size());result.samples.push_back({texture.index,sampler.index,input(x.input),x.lane,y.lane,x.divide});
   }else{
    require(i.opcode==Opcode::move||i.opcode==Opcode::multiply||i.opcode==Opcode::divide);const auto op=i.opcode==Opcode::move?0u:(i.opcode==Opcode::multiply?1u:2u);require(i.source_count==(op?2u:1u));instruction[0]=op;instruction[2]=encode(i.sources[0],d.mask);if(op)instruction[3]=encode(i.sources[1],d.mask);
    for(unsigned c=0;c<4;++c)if(d.mask&(1u<<c)){
     if(op==0)next[c]=coordinate(i.sources[0],c);
     else if(op==2){const auto left=coordinate(i.sources[0],c),right=coordinate(i.sources[1],c);if(left.valid&&right.valid&&!left.divide&&!right.divide&&left.input==right.input)next[c]={true,left.input,left.lane,right.lane+1};}
    }
   }
   if(d.kind==Register::temporary)for(unsigned c=0;c<4;++c)if(d.mask&(1u<<c))coordinates[d.index][c]=next[c];
   initialized[destination]|=d.mask;result.instructions.push_back(instruction);
  }
  require(initialized[4]==15);return result;
 }
};
}
