#pragma once
namespace nr::live {
struct Route {LUID physical_source{},physical_target{},indirect_source{},indirect_target{};UINT physical_source_id=0,physical_target_id=0,indirect_source_id=0,indirect_target_id=0;RECT physical{},indirect{};};
inline bool equal(LUID a,LUID b){return a.LowPart==b.LowPart && a.HighPart==b.HighPart;}
inline bool same(const Route &a,const Route &b){return equal(a.physical_source,b.physical_source)&&equal(a.physical_target,b.physical_target)&&equal(a.indirect_source,b.indirect_source)&&equal(a.indirect_target,b.indirect_target)&&a.physical_source_id==b.physical_source_id&&a.physical_target_id==b.physical_target_id&&a.indirect_source_id==b.indirect_source_id&&a.indirect_target_id==b.indirect_target_id&&EqualRect(&a.physical,&b.physical)&&EqualRect(&a.indirect,&b.indirect);}
inline Route separated(){
    constexpr UINT flags=QDC_ONLY_ACTIVE_PATHS|QDC_VIRTUAL_MODE_AWARE;
    for(unsigned attempt=0;attempt<4;++attempt){UINT pc=0,mc=0;require(GetDisplayConfigBufferSizes(flags,&pc,&mc)==ERROR_SUCCESS && pc==2 && mc<=256,"live requires one physical and one indirect display");
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);const auto status=QueryDisplayConfig(flags,&pc,paths.data(),&mc,modes.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER)continue;require(status==ERROR_SUCCESS && pc==2,"live topology query failed");Route result;unsigned physical=0,indirect=0;
        for(const auto &p:paths){const auto i=(p.flags&DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE)?p.sourceInfo.sourceModeInfoIdx:p.sourceInfo.modeInfoIdx;require(i<mc && modes[i].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE && (p.flags&DISPLAYCONFIG_PATH_ACTIVE),"live source mode unavailable");const auto &s=modes[i].sourceMode;require(s.width && s.width<=8192 && s.height && s.height<=8192,"live mode bounds rejected");RECT rect{s.position.x,s.position.y,s.position.x+LONG(s.width),s.position.y+LONG(s.height)};
            if(p.targetInfo.outputTechnology==DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED){++indirect;result.indirect_source=p.sourceInfo.adapterId;result.indirect_source_id=p.sourceInfo.id;result.indirect_target=p.targetInfo.adapterId;result.indirect_target_id=p.targetInfo.id;result.indirect=rect;}
            else{++physical;result.physical_source=p.sourceInfo.adapterId;result.physical_source_id=p.sourceInfo.id;result.physical_target=p.targetInfo.adapterId;result.physical_target_id=p.targetInfo.id;result.physical=rect;}}
        require(physical==1 && indirect==1 && result.physical.left==0 && result.physical.top==0,"live physical layout rejected");
        require(!(equal(result.physical_source,result.indirect_source) && result.physical_source_id==result.indirect_source_id),"live clone source rejected");RECT overlap{};require(!IntersectRect(&overlap,&result.physical,&result.indirect),"live overlapping desktops rejected");return result;
    }throw std::runtime_error("live topology unstable");
}
}
