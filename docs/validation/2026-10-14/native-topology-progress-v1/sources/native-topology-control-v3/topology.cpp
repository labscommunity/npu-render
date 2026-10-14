#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include "session_gate.hpp"
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Topology {std::vector<DISPLAYCONFIG_PATH_INFO> paths;std::vector<DISPLAYCONFIG_MODE_INFO> modes;};
bool equal(LUID a,LUID b){return a.LowPart==b.LowPart && a.HighPart==b.HighPart;}
bool same_target(const DISPLAYCONFIG_PATH_INFO &a,const DISPLAYCONFIG_PATH_INFO &b){return equal(a.targetInfo.adapterId,b.targetInfo.adapterId) && a.targetInfo.id==b.targetInfo.id;}
bool same_source(const DISPLAYCONFIG_PATH_INFO &a,const DISPLAYCONFIG_PATH_INFO &b){return equal(a.sourceInfo.adapterId,b.sourceInfo.adapterId) && a.sourceInfo.id==b.sourceInfo.id;}
Topology query(UINT flags){
 flags|=QDC_VIRTUAL_MODE_AWARE;
 for(unsigned attempt=0;attempt<4;++attempt){UINT pc=0,mc=0;const auto sizing=GetDisplayConfigBufferSizes(flags,&pc,&mc);std::printf("{\"query_flags\":%u,\"size_status\":%ld,\"path_capacity\":%u,\"mode_capacity\":%u}\n",flags,sizing,pc,mc);std::fflush(stdout);require(sizing==ERROR_SUCCESS && pc<=4096 && mc<=8192,"topology size unavailable");Topology result;result.paths.resize(pc);result.modes.resize(mc);
  const auto code=QueryDisplayConfig(flags,&pc,result.paths.data(),&mc,result.modes.data(),nullptr);if(code==ERROR_INSUFFICIENT_BUFFER)continue;require(code==ERROR_SUCCESS,"topology query failed");result.paths.resize(pc);result.modes.resize(mc);return result;}
 throw std::runtime_error("topology did not stabilize");
}
const DISPLAYCONFIG_SOURCE_MODE &source(const Topology &t,const DISPLAYCONFIG_PATH_INFO &p){const auto i=(p.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)?p.sourceInfo.sourceModeInfoIdx:p.sourceInfo.modeInfoIdx;require(i<t.modes.size() && t.modes[i].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE,"source mode unavailable");return t.modes[i].sourceMode;}
void print(const char *phase,const Topology &t){
 std::printf("{\"phase\":\"%s\",\"paths\":%zu,\"modes\":%zu,\"observed_tick\":%llu}\n",phase,t.paths.size(),t.modes.size(),GetTickCount64());
 for(const auto &p:t.paths){const auto i=(p.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)?p.sourceInfo.sourceModeInfoIdx:p.sourceInfo.modeInfoIdx;const bool has=i<t.modes.size() && t.modes[i].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;DISPLAYCONFIG_SOURCE_MODE s{};if(has)s=t.modes[i].sourceMode;
 std::printf("{\"phase\":\"%s\",\"active\":%s,\"available\":%s,\"source_low\":%lu,\"source_high\":%ld,\"source_id\":%u,\"target_low\":%lu,\"target_high\":%ld,\"target_id\":%u,\"technology\":%u,\"source_mode_valid\":%s,\"width\":%u,\"height\":%u,\"x\":%ld,\"y\":%ld}\n",phase,p.flags&DISPLAYCONFIG_PATH_ACTIVE?"true":"false",p.targetInfo.targetAvailable?"true":"false",p.sourceInfo.adapterId.LowPart,p.sourceInfo.adapterId.HighPart,p.sourceInfo.id,p.targetInfo.adapterId.LowPart,p.targetInfo.adapterId.HighPart,p.targetInfo.id,UINT(p.targetInfo.outputTechnology),has?"true":"false",s.width,s.height,s.position.x,s.position.y);}
 std::fflush(stdout);
}
bool equivalent(const Topology &a,const Topology &b){
 if(a.paths.size()!=b.paths.size())return false;
 for(const auto &p:a.paths){bool found=false;for(const auto &q:b.paths)if(same_source(p,q)&&same_target(p,q)){
  const auto &x=source(a,p),&y=source(b,q);found=x.width==y.width&&x.height==y.height&&x.pixelFormat==y.pixelFormat&&x.position.x==y.position.x&&x.position.y==y.position.y&&p.targetInfo.rotation==q.targetInfo.rotation&&p.targetInfo.scaling==q.targetInfo.scaling&&p.targetInfo.refreshRate.Numerator==q.targetInfo.refreshRate.Numerator&&p.targetInfo.refreshRate.Denominator==q.targetInfo.refreshRate.Denominator;}
  if(!found)return false;}
 return true;
}
Topology extension(const Topology &before,const Topology &all){
 require(before.paths.size()==2,"exactly one physical and one indirect path required");size_t pi=2,ii=2;
 for(size_t i=0;i<2;++i){if(before.paths[i].targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED)ii=i;else pi=i;}
 require(pi<2&&ii<2,"physical and indirect paths required");const auto &physical=before.paths[pi],&indirect=before.paths[ii];const auto &original=source(before,physical);
 require(original.position.x==0&&original.position.y==0,"physical source must be primary before extension");
 DISPLAYCONFIG_PATH_INFO candidate{};bool found=false;
 for(const auto &p:all.paths)if(same_target(p,indirect)&&p.targetInfo.targetAvailable&&!same_source(p,physical)){candidate=p;found=true;break;}
 require(found,"no independent indirect source path available");
 Topology desired=before;desired.paths={physical};DISPLAYCONFIG_MODE_INFO mode{};mode.infoType=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;mode.id=candidate.sourceInfo.id;mode.adapterId=candidate.sourceInfo.adapterId;mode.sourceMode.width=1024;mode.sourceMode.height=768;mode.sourceMode.pixelFormat=DISPLAYCONFIG_PIXELFORMAT_32BPP;mode.sourceMode.position={LONG(original.width),0};
 if(candidate.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE){candidate.sourceInfo.sourceModeInfoIdx=UINT(desired.modes.size());candidate.sourceInfo.cloneGroupId=DISPLAYCONFIG_PATH_CLONE_GROUP_INVALID;candidate.targetInfo.targetModeInfoIdx=DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID;candidate.targetInfo.desktopModeInfoIdx=DISPLAYCONFIG_PATH_DESKTOP_IMAGE_IDX_INVALID;}else{candidate.sourceInfo.modeInfoIdx=UINT(desired.modes.size());candidate.targetInfo.modeInfoIdx=DISPLAYCONFIG_PATH_MODE_IDX_INVALID;}candidate.targetInfo.refreshRate={60,1};candidate.targetInfo.rotation=DISPLAYCONFIG_ROTATION_IDENTITY;candidate.targetInfo.scaling=DISPLAYCONFIG_SCALING_IDENTITY;candidate.targetInfo.scanLineOrdering=DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;candidate.flags|=DISPLAYCONFIG_PATH_ACTIVE;
 desired.modes.push_back(mode);desired.paths.push_back(candidate);return desired;
}
LONG set(Topology &t,UINT action){return SetDisplayConfig(UINT(t.paths.size()),t.paths.data(),UINT(t.modes.size()),t.modes.data(),SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_VIRTUAL_MODE_AWARE|action);}
}
int wmain(int argc,wchar_t **argv)try{
 require(argc==2||argc==3,"expected --inspect, --validate-extend or --extend <1..180 seconds>");require(nr::present::current_console_default(),"unlocked Default console required");
 const auto before=query(QDC_ONLY_ACTIVE_PATHS),all=query(QDC_ALL_PATHS);print("before",before);print("available",all);
 if(argc==2&&!std::wcscmp(argv[1],L"--inspect"))return 0;
 const bool apply=argc==3&&!std::wcscmp(argv[1],L"--extend");require(apply||(argc==2&&!std::wcscmp(argv[1],L"--validate-extend")),"mode rejected");
 wchar_t *end=nullptr;const auto seconds=apply?std::wcstoul(argv[2],&end,10):0;require(!apply||(end!=argv[2]&&!*end&&seconds>=1&&seconds<=180),"duration rejected");
 auto desired=extension(before,all);print("requested",desired);const auto validation=set(desired,SDC_VALIDATE|SDC_ALLOW_CHANGES);std::printf("{\"validation_status\":%ld,\"display_changed\":false}\n",validation);std::fflush(stdout);require(validation==ERROR_SUCCESS,"extension validation failed");if(!apply)return 0;
 auto restore=before;bool passed=false;
 try{
  require(nr::present::current_console_default(),"console changed before application");const auto code=set(desired,SDC_APPLY|SDC_ALLOW_CHANGES);std::printf("{\"apply_status\":%ld}\n",code);std::fflush(stdout);require(code==ERROR_SUCCESS,"extension application failed");
  const auto active=query(QDC_ONLY_ACTIVE_PATHS);print("extended",active);require(active.paths.size()==2,"extended path count mismatch");
  Topology expected_physical,actual_physical;for(const auto &p:before.paths)if(p.targetInfo.outputTechnology!=DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED)expected_physical.paths.push_back(p);expected_physical.modes=before.modes;
  for(const auto &p:active.paths)if(p.targetInfo.outputTechnology!=DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED)actual_physical.paths.push_back(p);actual_physical.modes=active.modes;
  require(equivalent(expected_physical,actual_physical),"physical path changed");require(!same_source(active.paths[0],active.paths[1]),"sources still cloned");
  const auto &a=source(active,active.paths[0]),&b=source(active,active.paths[1]);require(a.position.x+LONG(a.width)<=b.position.x||b.position.x+LONG(b.width)<=a.position.x,"logical desktops overlap");
  std::printf("{\"extension_verified\":true,\"physical_path_preserved\":true,\"seconds\":%lu}\n",seconds);std::fflush(stdout);
  for(unsigned i=0;i<seconds*10;++i){require(nr::present::current_console_default(),"interactive desktop changed");Sleep(100);}passed=true;
 }catch(const std::exception &e){std::fprintf(stderr,"extension trial: %s\n",e.what());}
 const auto status=set(restore,SDC_APPLY);const auto after=query(QDC_ONLY_ACTIVE_PATHS);print("restored",after);const bool exact=status==ERROR_SUCCESS&&equivalent(before,after);
 std::printf("{\"completed\":true,\"passed\":%s,\"restore_status\":%ld,\"topology_restored_exactly\":%s,\"desktop_offloaded\":false}\n",passed&&exact?"true":"false",status,exact?"true":"false");return passed&&exact?0:1;
}catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
