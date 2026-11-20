#pragma once
#include <obs.h>
#include <algorithm>
#include <charconv>
#include <memory>
#include <string>
#include <string_view>

namespace nexus {
inline constexpr std::string_view live_prefix="obs-source:";
inline bool is_live(std::string_view key){return key.starts_with(live_prefix);}
inline std::string live_uuid(std::string_view key){
    auto value=key.substr(live_prefix.size());
    return std::string(value.substr(0,value.find(':')));
}
inline int live_seconds(std::string_view key){
    auto boundary=key.rfind(':');
    int seconds=8;
    if(boundary!=std::string_view::npos){
        auto value=key.substr(boundary+1);
        auto result=std::from_chars(value.data(),value.data()+value.size(),seconds);
        if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size())return 8;
    }
    return std::clamp(seconds,1,3600);
}
inline bool usable_live_source(obs_source_t *source){
    if(!source||obs_source_removed(source)||!(obs_source_get_output_flags(source)&OBS_SOURCE_VIDEO))return false;
    auto playlist=[](obs_source_t *candidate){return std::string_view(obs_source_get_id(candidate))=="obs_playlist_nexus";};
    if(playlist(source))return false;
    bool nested=false;
    obs_source_enum_full_tree(source,[](obs_source_t *,obs_source_t *child,void *data){
        if(std::string_view(obs_source_get_id(child))=="obs_playlist_nexus")*static_cast<bool *>(data)=true;
    },&nested);
    return !nested;
}
using SourceRef=std::unique_ptr<obs_source_t,decltype(&obs_source_release)>;
inline SourceRef resolve_live(std::string_view key){return {obs_get_source_by_uuid(live_uuid(key).c_str()),obs_source_release};}
class LiveEntry {
    obs_weak_source_t *weak;
public:
    explicit LiveEntry(obs_source_t *source):weak(obs_source_get_weak_source(source)){obs_source_inc_active(source);}
    LiveEntry(const LiveEntry &)=delete;
    LiveEntry &operator=(const LiveEntry &)=delete;
    SourceRef source() const {return {obs_weak_source_get_source(weak),obs_source_release};}
    ~LiveEntry(){auto ref=source();if(ref)obs_source_dec_active(ref.get());obs_weak_source_release(weak);}
};
}
