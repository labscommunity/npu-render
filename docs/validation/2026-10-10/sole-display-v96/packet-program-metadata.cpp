#include "../../native/windows/registered_lab/shader_ir.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char **argv){try{if(argc!=3)return 2;std::ifstream f(argv[2],std::ios::binary);std::vector<uint32_t>w;uint32_t x;while(f.read(reinterpret_cast<char*>(&x),4))w.push_back(x);const auto p=std::string(argv[1])=="vs"?nr::shader::parse_vertex(w.data(),w.size()):nr::shader::parse_pixel(w.data(),w.size());std::cout<<"{\"constants\":[";for(unsigned n=0;n<14;++n)std::cout<<(n?",":"")<<p.constants[n];std::cout<<"],\"inputs\":[";for(unsigned n=0;n<16;++n)std::cout<<(n?",":"")<<p.inputs[n];std::cout<<"]}\n";return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}
