#include "vertex_executor.hpp"
#include "shader_reference.hpp"
#include "vertex_id_fixture.hpp"
#include <cstdio>
using namespace nr::shader;
int main(int argc,char **argv)try{
 if(argc!=2)throw std::runtime_error("backend required");unsigned failures=0;
 const auto &w=nr::registered_lab::test_fixture::vertex_id_quad_words;const auto p=parse_vertex(w,std::size(w));
 for(unsigned count:{4u,6u,1u,2u,3u,8u,16u,32u,64u}){
  std::vector<VertexRegisters> input(count);for(unsigned n=0;n<count;++n)input[n][0][0]=n%4;
  try{VertexExecutor e(p,count,argv[1]);auto result=e.execute(input,{});unsigned wrong=0;
   for(unsigned n=0;n<count;++n){auto expected=reference_vertex(p,input[n],{});for(unsigned c=0;c<4;++c)if(result.vertices[n][0][c]!=expected[0][c]){++wrong;std::printf("count=%u row=%u lane=%u actual=%08x expected=%08x\n",count,n,c,result.vertices[n][0][c],expected[0][c]);}}
   bool invalid_rejected=false;input[0][0][0]=4;try{(void)e.execute(input,{});}catch(const std::runtime_error &){invalid_rejected=true;}
   bool passed=!wrong && invalid_rejected && result.execution_device.rfind(argv[1],0)==0;if(!passed)++failures;
   std::printf("{\"count\":%u,\"wrong_components\":%u,\"invalid_rejected\":%s,\"passed\":%s}\n",count,wrong,invalid_rejected?"true":"false",passed?"true":"false");
  }catch(const std::exception &e){++failures;std::printf("count=%u rejected valid inputs: %s\n",count,e.what());}
  std::fflush(stdout);
 }
 return failures?1:0;
}catch(const std::exception &e){std::fprintf(stderr,"diagnostic: %s\n",e.what());return 1;}
