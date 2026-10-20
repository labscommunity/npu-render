#include "command_batch.hpp"
#include <openvino/openvino.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
using nr::batch::Resource;
using nr::batch::Version;
using nr::batch::Ticket;
using Pixels=std::vector<uint32_t>;
using Rectangle=std::array<uint32_t,8>;
static void require(bool ok){if(!ok)throw std::runtime_error("batch transaction contract");}
static std::string digest(const void *data,size_t size){
 require(size<=(std::numeric_limits<ULONG>::max)());BCRYPT_ALG_HANDLE algorithm=nullptr;
 require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);unsigned char bytes[32]{};
 const auto code=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),ULONG(size),bytes,32);BCryptCloseAlgorithmProvider(algorithm,0);require(code>=0);
 std::ostringstream result;for(auto byte:bytes)result<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);return result.str();
}
static uint32_t get(const unsigned char *p){uint32_t n;std::memcpy(&n,p,4);return n;}
class Npu {
 ov::Core core;ov::CompiledModel model;ov::InferRequest request;std::string blob;
public:
 uint64_t generations=0,submissions=0;
 explicit Npu(const std::filesystem::path &path){
  std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f)&&f.tellg()==73857);blob.resize(73857);f.seekg(0);f.read(blob.data(),std::streamsize(blob.size()));require(bool(f));
  require(digest(blob.data(),blob.size())=="e4a863410d6e629cd4f5ebcd955201a9024b2e75e6e6f6b19366fe582f6aaf26");restart();
 }
 void restart(){
  request=ov::InferRequest{};model=ov::CompiledModel{};
  std::istringstream stream(blob,std::ios::binary);model=core.import_model(stream,"NPU");const auto devices=model.get_property(ov::execution_devices);require(devices.size()==1&&(devices[0]=="NPU"||devices[0]=="NPU.0"));request=model.create_infer_request();
  require(model.inputs().size()==1&&model.outputs().size()==1&&request.get_input_tensor().get_shape()==ov::Shape({6,131072})&&request.get_output_tensor().get_shape()==ov::Shape({6,131072})&&request.get_input_tensor().get_element_type()==ov::element::f16&&request.get_output_tensor().get_element_type()==ov::element::f16);++generations;
 }
 Pixels render(const Pixels &initial,uint32_t width,uint32_t height,uint32_t format,const std::vector<Rectangle> &commands,bool interrupt_after_first){
  require(width&&height&&width<=8192&&height<=8192&&uint64_t(width)*height<=8388576&&initial.size()==size_t(width)*height&&(format==28||format==87)&&!commands.empty()&&commands.size()<=32);
  Pixels result(initial.size());constexpr uint32_t span=262144,capacity=65264;
  const auto area=uint32_t(initial.size());uint64_t calls=0;
  for(uint32_t first=0;first<area;first+=capacity*6){
   auto *input=static_cast<unsigned char*>(request.get_input_tensor().data());std::memset(input,0,span*6);
   const auto total=(std::min)(area-first,capacity*6),stride=(total+5)/6;
   std::array<std::pair<uint32_t,uint32_t>,6> ranges{};
   for(uint32_t plane=0;plane<6;++plane){
    auto *data=input+plane*span;const auto at=first+(std::min)(total,plane*stride),count=(std::min)(stride,first+total-at);ranges[plane]={at,count};
    const std::array<uint32_t,9> header{0x4e524351,1,count,at,width,height,uint32_t(commands.size()),format,span};std::memcpy(data,header.data(),sizeof(header));
    for(size_t i=0;i<commands.size();++i)std::memcpy(data+64+i*32,commands[i].data(),32);
    if(count)std::memcpy(data+1088,initial.data()+at,count*4);
   }
   request.start_async();require(request.wait_for(15000));++submissions;++calls;
   const auto *output=static_cast<const unsigned char*>(request.get_output_tensor().data());
   for(uint32_t plane=0;plane<6;++plane){const auto *data=output+plane*span;const auto [at,count]=ranges[plane];
    require(get(data)==0x4e52434f&&get(data+4)==count&&get(data+8)==commands.size()&&get(data+12)<=count*commands.size());
    if(count)std::memcpy(result.data()+at,data+16,count*4);
   }
   // Simulated exception only after the NPU has produced a partial surface.
   // The local result is never installed as the committed checkpoint.
   if(interrupt_after_first&&calls==1)throw std::runtime_error("injected after partial NPU output");
  }
  return result;
 }
};
class Transaction {
 nr::batch::Ledger ledger;
 Resource resource{7,1,0};uint64_t context;uint32_t width,height,format;
 std::shared_ptr<const Pixels> checkpoint,pinned;
 Ticket active{};bool pending=false;
 std::vector<Rectangle> decoded;
 std::vector<Version> completions;
public:
 Transaction(uint64_t id,uint32_t w,uint32_t h,uint32_t f,const Pixels &initial):ledger(id),context(id),width(w),height(h),format(f),checkpoint(std::make_shared<const Pixels>(initial)){
  require(w&&h&&w<=8192&&h<=8192&&uint64_t(w)*h<=8388576&&initial.size()==size_t(w)*h&&(f==28||f==87));ledger.import_resource({context,resource,1},initial.size()*4);
 }
 nr::batch::Request request(const std::vector<Rectangle> &commands,uint64_t sequence=1)const{
  require(!commands.empty()&&commands.size()<=32);nr::batch::Request r{context,sequence,{ledger.committed(resource)},{}};auto before=r.imports[0];
  for(const auto &rectangle:commands){auto after=before;++after.number;nr::batch::Command command{{},before,after,std::vector<uint8_t>(32)};std::memcpy(command.state.data(),rectangle.data(),32);r.commands.push_back(std::move(command));before=after;}return r;
 }
 Ticket prepare(const nr::batch::Request &r){
  require(!pending&&r.imports.size()==1&&r.imports[0]==ledger.committed(resource)&&!r.commands.empty()&&r.commands.size()<=32);
  std::vector<Rectangle> rectangles;std::vector<Version> outputs;
  for(const auto &command:r.commands){
   require(command.reads.empty()&&command.before.resource==resource&&command.after.resource==resource&&command.state.size()==32);Rectangle q{};std::memcpy(q.data(),command.state.data(),32);
   require(q[0]<=q[2]&&q[1]<=q[3]&&q[2]<=width&&q[3]<=height&&q[5]<=1&&q[6]&&q[6]<=15&&!q[7]);rectangles.push_back(q);outputs.push_back(command.after);
  }
  active=ledger.prepare(r);decoded=std::move(rectangles);completions=std::move(outputs);pinned=checkpoint;pending=true;return active;
 }
 void execute(Npu &npu,bool interrupt=false){
  require(pending);auto pixels=npu.render(*pinned,width,height,format,decoded,interrupt);require(pixels.size()==checkpoint->size());auto next=std::make_shared<const Pixels>(std::move(pixels));
  // These commands only consume the immediately preceding target state. There
  // are no reads of older intermediate versions, so their storage may be fused.
  ledger.complete(active,completions);checkpoint=std::move(next);pinned.reset();pending=false;
 }
 Ticket retry(){require(pending);active=ledger.retry(active);return active;}
 void abandon(){require(pending);ledger.abandon(active);pinned.reset();pending=false;}
 void reject_late(Ticket old){bool rejected=false;try{ledger.complete(old,completions);}catch(const std::invalid_argument&){rejected=true;}require(rejected);}
 uint64_t version()const{return ledger.committed(resource).number;}
 std::string checkpoint_hash()const{return digest(checkpoint->data(),checkpoint->size()*4);}
 const Pixels &read()const{require(ledger.read_ready(resource)==nr::batch::Readiness::ready);return *checkpoint;}
 bool blocked_read()const{try{(void)read();return false;}catch(const std::runtime_error&){return true;}}
};
static Pixels reference(Pixels image,uint32_t w,uint32_t format,const std::vector<Rectangle> &commands){
 for(const auto &q:commands)for(uint32_t y=q[1];y<q[3];++y)for(uint32_t x=q[0];x<q[2];++x){auto &packed=image[size_t(y)*w+x];
  for(uint32_t byte=0;byte<4;++byte){const auto channel=format==87&&byte<3?2-byte:byte;if(!(q[6]&(1u<<channel)))continue;
   const auto source=(q[4]>>(channel*8))&255;uint32_t value=source;
   if(q[5])value=((std::min)(65025u,source*255+((packed>>(byte*8))&255)*(255-(q[4]>>24)))+127)/255;
   packed=(packed&~(255u<<(byte*8)))|(value<<(byte*8));
  }
 }return image;
}
static uint32_t random_word(uint32_t &seed){seed=1664525u*seed+1013904223u;return seed;}
int wmain(int argc,wchar_t **argv){
 try{
  require(argc==2);const std::filesystem::path blob(argv[1]);uint32_t seed=91553;
  for(uint32_t scenario=0;scenario<4;++scenario){
   const uint32_t w=1024,h=768,format=scenario%2?87:28;Pixels initial(size_t(w)*h);for(auto &pixel:initial)pixel=random_word(seed);
   std::vector<Rectangle> commands;
   for(uint32_t n=0;n<32;++n){auto x0=random_word(seed)%(w+1),x1=random_word(seed)%(w+1),y0=random_word(seed)%(h+1),y1=random_word(seed)%(h+1);if(x0>x1)std::swap(x0,x1);if(y0>y1)std::swap(y0,y1);if(n%3==0){x0=y0=0;x1=w;y1=h;}
    commands.push_back({x0,y0,x1,y1,random_word(seed),n%2,1+n%15,0});}
   const auto expected=reference(initial,w,format,commands);Transaction transaction(100+scenario,w,h,format,initial);const auto before=transaction.checkpoint_hash();auto request=transaction.request(commands);const auto ticket=transaction.prepare(request);
   // Mutating caller state after preparation cannot change the admitted batch.
   request.commands[0].state.assign(32,0);std::fill(initial.begin(),initial.end(),0);
   Npu npu(blob);bool failed=false,retried=false,abandoned=false;
   require(transaction.blocked_read()&&transaction.version()==1);
   if(scenario>=2){try{transaction.execute(npu,true);}catch(const std::runtime_error &error){require(std::string(error.what())=="injected after partial NPU output");failed=true;}
    require(failed&&npu.submissions==1&&transaction.version()==1&&transaction.checkpoint_hash()==before&&transaction.blocked_read());
    if(scenario==3){transaction.abandon();abandoned=true;require(transaction.version()==1&&transaction.checkpoint_hash()==before&&!transaction.blocked_read());}
    else{npu.restart();const auto current=transaction.retry();require(!(current==ticket));transaction.reject_late(ticket);retried=true;}
   }
   if(!abandoned){transaction.execute(npu);require(transaction.read()==expected&&transaction.version()==33&&!transaction.blocked_read());}
   require(npu.generations==(retried?2u:1u)&&npu.submissions==(abandoned?1u:retried?4u:3u));
   if(scenario==1){
    std::reverse(commands.begin(),commands.end());const auto expected_next=reference(expected,w,format,commands);
    transaction.prepare(transaction.request(commands,2));transaction.execute(npu);
    require(transaction.version()==65&&transaction.read()==expected_next&&npu.submissions==6);
   }
   std::cout<<"{\"scenario\":"<<scenario<<",\"passed\":true,\"execution_device\":\"NPU\",\"injected_after_partial_output\":"<<(failed?"true":"false")<<",\"retried\":"<<(retried?"true":"false")<<",\"abandoned\":"<<(abandoned?"true":"false")<<",\"provider_generations\":"<<npu.generations<<",\"provider_submissions\":"<<npu.submissions<<",\"committed_version\":"<<transaction.version()<<",\"committed_pixels_sha256\":\""<<transaction.checkpoint_hash()<<"\",\"cpu_fallback\":false,\"desktop_tested\":false}\n";
  }
  return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
