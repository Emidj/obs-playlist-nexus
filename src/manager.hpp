#pragma once
#include <obs.h>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
namespace nexus {
struct Frame;
struct ClipMotion {
    float offset=0, zoom=1, opacity=1;
};
inline ClipMotion clip_motion(int kind,float progress) {
    const float p=std::clamp(progress,0.0f,1.0f);
    const float t=p*p*(3-2*p);
    return {kind==2?-(1-t):kind==3?1-t:0,kind==4?0.65f+0.35f*t:1,kind==1?t:1};
}
struct PreviewFrame {
    std::shared_ptr<const Frame> incoming,outgoing;
    ClipMotion motion;
};
struct GroupState {
    int by=0;
    std::string separator="_";
    std::vector<std::string> labels,manual;
    std::map<std::string,int> quotas;
    bool operator==(const GroupState &) const=default;
};
struct ManagerState {
    std::vector<std::string> files;
    std::string current, next, error;
    bool random=false, paused=false, hidden=true, ready=false, exhausted=false, navigating=false;
    int order=0;
    GroupState groups;
    int64_t current_duration=0, next_duration=0;
    uint64_t transitions=0, failures=0;
};
struct ManagerSink {
    virtual ~ManagerSink()=default;
    virtual void changed()=0;
    virtual void frame_changed()=0;
    virtual void closed()=0;
};
// Call only while holding a strong obs_source_t reference. Snapshot is read-only;
// the UI never consumes decoder queues or changes the presentation clock.
struct ManagerBackend {
    virtual ~ManagerBackend()=default;
    virtual ManagerState snapshot()=0;
    virtual PreviewFrame preview_frame()=0;
    virtual void subscribe(std::shared_ptr<ManagerSink>)=0;
    virtual void unsubscribe(ManagerSink *)=0;
    virtual void navigate(int direction)=0;
    virtual void jump(size_t index)=0;
};
void open_manager(obs_source_t *source,ManagerBackend *backend);
void shutdown_managers();
}
