#include "native_admission.hpp"
#include <cstdio>
int main()try{
    if(nr::indirect::current_host_identity())return 1;
    unsigned cases=0;
    for(const DWORD limit:{0ul,1ul,10000ul,10001ul}){
        const auto before=GetTickCount64();bool rejected=false;try{auto admission=nr::indirect::await_native_admission(limit);(void)admission;}catch(const std::exception&){rejected=true;}
        if(!rejected || GetTickCount64()-before>1000)return 1;++cases;
    }
    std::printf("{\"native_indirect_guard_contract\":true,\"passed\":true,\"non_host_rejection_cases\":%u,\"actual_umdf_execution\":false,\"driver_installed\":false}\n",cases);return 0;
}catch(...){return 1;}
