#include <obs-module.h>
#include <obs-hotkey.h>
#include <util/platform.h>
#include "decoder.hpp"
#include "timeline.hpp"
#include "manager.hpp"
#include "balanced_order.hpp"
#include "grouping.hpp"
#include "localization.hpp"
#include "live_entry.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
#include <random>
#include <tuple>
#include <optional>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) { return "OBS Playlist NEXUS video playlist source"; }

namespace {
struct Source : nexus::ManagerBackend {
    std::array<nexus::Decoder, 2> decoders;
    std::mutex control;
    std::vector<std::string> files;
    size_t index = 0, next = 0;
    int active = 0;
    std::array<std::shared_ptr<nexus::LiveEntry>,2> live_slots;
    std::array<int64_t,2> live_requested{-1,-1};
    std::atomic<std::shared_ptr<nexus::LiveEntry>> live_output;
    bool live_ready=false,rendering_live=false;
    bool random = false, paused = false, hidden = true;
    int order_mode=0;
    nexus::BalancedOrder balanced;
    std::shared_ptr<const nexus::GroupState> groups_view=std::make_shared<const nexus::GroupState>();
    nexus::Timeline timeline;
    std::mt19937 rng{std::random_device{}()};
    std::atomic<std::shared_ptr<nexus::Frame>> displayed;
    std::shared_ptr<nexus::Frame> uploaded;
    std::atomic<bool> has_files{false};
    std::shared_ptr<const std::vector<std::string>> files_view=std::make_shared<const std::vector<std::string>>();
    gs_texture_t *texture = nullptr;
    struct ClipTransition {
        std::shared_ptr<nexus::Frame> outgoing;
        std::shared_ptr<nexus::LiveEntry> outgoing_live;
        int kind;
        int64_t duration;
    };
    int transition_kind=0;
    int64_t transition_duration=400000000;
    std::atomic<std::shared_ptr<ClipTransition>> clip_transition;
    std::atomic<int64_t> transition_elapsed{0};
    gs_texture_t *outgoing_texture=nullptr;
    gs_texrender_t *outgoing_live_target=nullptr,*incoming_live_target=nullptr;
    gs_effect_t *fade_effect=nullptr;
    bool fade_initialized=false;
    std::weak_ptr<ClipTransition> uploaded_transition;
    size_t consecutive_errors = 0;
    bool exhausted = false;
    std::vector<obs_hotkey_id> hotkeys;
    void set_paused(bool value) {
        auto *settings=obs_source_get_settings(owner);
        obs_data_set_bool(settings,"paused",value);obs_source_update(owner,settings);obs_data_release(settings);
    }
    void register_hotkeys() {
        hotkeys.push_back(obs_hotkey_register_source(owner,"nexus_playlist.play",nexus::localized(nexus::Text::PlaylistNexusPlay),[](void *data,obs_hotkey_id,obs_hotkey_t *,bool pressed){if(pressed)static_cast<Source *>(data)->set_paused(false);},this));
        hotkeys.push_back(obs_hotkey_register_source(owner,"nexus_playlist.pause",nexus::localized(nexus::Text::PlaylistNexusPause),[](void *data,obs_hotkey_id,obs_hotkey_t *,bool pressed){if(pressed)static_cast<Source *>(data)->set_paused(true);},this));
        hotkeys.push_back(obs_hotkey_register_source(owner,"nexus_playlist.next",nexus::localized(nexus::Text::PlaylistNexusNext),[](void *data,obs_hotkey_id,obs_hotkey_t *,bool pressed){if(pressed)static_cast<Source *>(data)->navigate(1);},this));
        hotkeys.push_back(obs_hotkey_register_source(owner,"nexus_playlist.previous",nexus::localized(nexus::Text::PlaylistNexusPrevious),[](void *data,obs_hotkey_id,obs_hotkey_t *,bool pressed){if(pressed)static_cast<Source *>(data)->navigate(-1);},this));
    }
    obs_source_t *owner=nullptr;
    std::string current_path, prepared_path, manual_target, last_error;
    bool manual_previous=false, pause_hidden=true, restart_show=false;
    std::vector<std::string> history;
    std::shared_ptr<nexus::ManagerSink> sink;
    std::atomic<bool> closing{false};
    uint64_t revision=0;
    using UiKey=std::tuple<uint64_t,uint64_t,uint64_t,bool,bool,bool,bool,bool,bool,bool>;
    std::optional<UiKey> last_ui;
    explicit Source(obs_source_t *source):owner(source) {
        for(auto &decoder:decoders) decoder.set_changed([this] {
            if(closing) return;
            std::lock_guard<std::mutex> lock(control); publish_locked();
        });
    }
    void publish_locked() {
        if(!sink || closing) return;
        const UiKey key{revision,timeline.transitions,timeline.continuity_failures,paused,hidden,
            nexus::is_live(prepared_path)?live_ready:decoders[1-active].prepared(),exhausted,!manual_target.empty(),bool(displayed.load())||bool(live_output.load()),decoders[1-active].failed()};
        if(!last_ui || *last_ui!=key) { last_ui=key; sink->changed(); }
    }
    nexus::ManagerState snapshot() override {
        std::unique_lock<std::mutex> lock(control);
        nexus::ManagerState state;
        auto view=files_view;
        auto group_view=groups_view;state.order=order_mode;
        state.current=current_path; state.next=prepared_path; state.error=last_error;
        state.random=random; state.paused=paused; state.hidden=hidden&&pause_hidden;
        state.ready=nexus::is_live(prepared_path)?live_ready:decoders[1-active].prepared(); state.exhausted=exhausted;
        state.navigating=!manual_target.empty();
        state.current_duration=decoders[active].duration_hint(); state.next_duration=decoders[1-active].duration_hint();
        if(nexus::is_live(current_path))state.current_duration=int64_t(nexus::live_seconds(current_path))*1000000000;
        if(nexus::is_live(prepared_path))state.next_duration=int64_t(nexus::live_seconds(prepared_path))*1000000000;
        state.transitions=timeline.transitions; state.failures=timeline.continuity_failures;
        if(state.error.empty() && decoders[1-active].failed() && !state.ready) state.error=nexus::localized(nexus::Text::CouldNotPrepareTheNextVideo);
        lock.unlock(); state.files=*view;state.groups=*group_view;
        return state;
    }
    nexus::PreviewFrame preview_frame() override {
        auto transition=clip_transition.load();
        auto incoming=displayed.load();
        if(!transition||!transition->outgoing||live_output.load())return {incoming,{},{}};
        const float progress=float(transition_elapsed.load())/transition->duration;
        return {incoming,transition->outgoing,nexus::clip_motion(transition->kind,progress)};
    }
    void subscribe(std::shared_ptr<nexus::ManagerSink> value) override {
        std::lock_guard<std::mutex> lock(control); sink=std::move(value); last_ui.reset(); publish_locked();
    }
    void unsubscribe(nexus::ManagerSink *value) override {
        std::lock_guard<std::mutex> lock(control); if(sink.get()==value) sink.reset();
    }
    void request_locked(int slot,const std::string &path) {
        live_slots[slot].reset();live_requested[slot]=-1;
        decoders[slot].request(nexus::is_live(path)?"":path);
    }
    nexus::Sample sample_locked(int slot,const std::string &path,int64_t local,int64_t now) {
        if(!nexus::is_live(path))return decoders[slot].sample(local);
        nexus::Sample result;
        result.end=int64_t(nexus::live_seconds(path))*1000000000;
        if(live_requested[slot]<0)live_requested[slot]=now;
        auto &entry=live_slots[slot];
        auto source=entry?entry->source():nexus::resolve_live(path);
        if(source&&!nexus::usable_live_source(source.get())){result.failed=true;return result;}
        if(source&&!entry)entry=std::make_shared<nexus::LiveEntry>(source.get());
        result.ready=source&&obs_source_get_width(source.get())>0&&obs_source_get_height(source.get())>0;
        result.failed=!result.ready&&now-live_requested[slot]>=2000000000;
        result.ended=result.ready&&local>=result.end;
        return result;
    }
    void prepare_locked(const std::string &path) {
        if(prepared_path==path) return;
        prepared_path=path;live_ready=false;request_locked(1-active,path);
    }
    void jump_locked(const std::string &path,bool previous=false) {
        if(path.empty() || !manual_target.empty()) return;
        manual_target=path; manual_previous=previous; exhausted=false; last_error.clear();
        prepare_locked(path); ++revision; publish_locked();
    }
    void jump(size_t position) override {
        std::lock_guard<std::mutex> lock(control); if(position<files.size()) jump_locked(files[position]);
    }
    void navigate(int direction) override {
        std::lock_guard<std::mutex> lock(control);
        if(files.empty() || !manual_target.empty()) return;
        if(direction<0) {
            while(!history.empty()) {
                auto path=history.back(); history.pop_back();
                if(std::find(files.begin(),files.end(),path)!=files.end()) { jump_locked(path,true); return; }
            }
            jump_locked(files[index<files.size() ? (index+files.size()-1)%files.size() : 0],true);
        } else jump_locked(prepared_path.empty() ? files[following()] : prepared_path);
    }
    void commit_locked(nexus::Sample sample,int64_t now,bool manual,int64_t duration=0) {
        if(!manual_previous && !current_path.empty()) {
            if(history.size()>=128) history.erase(history.begin());
            history.push_back(current_path);
        }
        const auto incoming=prepared_path;
        auto outgoing=displayed.load();
        auto outgoing_live=live_output.load();
        clip_transition.store({});
        if(transition_kind&&(outgoing_live||(!nexus::is_live(current_path)&&outgoing))&&
            (sample.frame||live_slots[1-active])&&
            !(paused||(hidden&&pause_hidden))&&incoming!=current_path){
            transition_elapsed=0;
            clip_transition.store(std::make_shared<ClipTransition>(ClipTransition{
                outgoing_live?nullptr:outgoing,outgoing_live,transition_kind,transition_duration}));
        }
        if(manual) {
            timeline.origin=now; timeline.paused_at=(paused||(hidden&&pause_hidden))?now:-1;
            timeline.stalled=false; ++timeline.transitions;
        } else timeline.switch_clip(now,duration);
        active=1-active; current_path=incoming;
        auto found=std::find(files.begin(),files.end(),current_path);
        index=found==files.end()?files.size():size_t(found-files.begin());
        if(order_mode==2&&index<files.size())balanced.presented(index);
        if(sample.frame) displayed=std::move(sample.frame);
        const auto previous_live=live_output.exchange(live_slots[active]);
        if((sample.ready||previous_live)&&sink)sink->frame_changed();
        manual_target.clear(); manual_previous=false; consecutive_errors=0;
        prepared_path.clear(); next=following(); prepare_locked(files.empty()?"":files[next]);
        ++revision; publish_locked();
        blog(LOG_INFO,"[Playlist NEXUS] transition=%llu continuity_failures=%llu clip=%s",
            (unsigned long long)timeline.transitions,(unsigned long long)timeline.continuity_failures,current_path.c_str());
    }
    size_t following() {
        if(order_mode==2)return balanced.peek().value_or(0);
        if(index>=files.size()) return 0;
        if (files.size() < 2) return 0;
        if (!random) return (index + 1) % files.size();
        std::uniform_int_distribution<size_t> pick(1, files.size() - 1);
        return (index + pick(rng)) % files.size();
    }
    void configure(obs_data_t *settings) {
        std::vector<std::string> incoming;
        auto group_config=std::make_shared<nexus::GroupState>();
        group_config->by=std::clamp(int(obs_data_get_int(settings,"group_by")),0,3);
        if(obs_data_has_user_value(settings,"group_separator"))group_config->separator=obs_data_get_string(settings,"group_separator");
        auto *quotas=obs_data_get_array(settings,"group_quotas");
        for(size_t i=0;i<obs_data_array_count(quotas);++i){
            auto *item=obs_data_array_item(quotas,i);
            const auto count=std::clamp(obs_data_get_int(item,"count"),1LL,99LL);
            group_config->quotas[obs_data_get_string(item,"key")]=int(count);
            obs_data_release(item);
        }
        obs_data_array_release(quotas);
        std::vector<std::string> group_keys;
        auto *list = obs_data_get_array(settings, "files");
        for (size_t i = 0; i < obs_data_array_count(list); ++i) {
            auto *item = obs_data_array_item(list, i);
            std::string path = obs_data_get_string(item, "value");
            if (!path.empty()) {
                const std::string manual=obs_data_get_string(item,"manual_group");
                auto label=nexus::is_live(path)?(group_config->by?QString::fromUtf8(manual.c_str()).trimmed():QString()):nexus::groupLabel(QString::fromUtf8(path.c_str()),group_config->by,QString::fromUtf8(group_config->separator.c_str()),QString::fromUtf8(manual.c_str()));
                group_config->manual.push_back(manual);group_config->labels.push_back(label.toUtf8().toStdString());
                group_keys.push_back(label.toCaseFolded().toUtf8().toStdString());incoming.push_back(std::move(path));
            }
            obs_data_release(item);
        }
        obs_data_array_release(list);
        auto view=std::make_shared<const std::vector<std::string>>(incoming);
        std::lock_guard<std::mutex> lock(control);
        paused = obs_data_get_bool(settings, "paused");
        transition_kind=int(std::clamp(obs_data_get_int(settings,"transition_kind"),0LL,4LL));
        transition_duration=std::clamp(obs_data_get_int(settings,"transition_ms"),50LL,2000LL)*1000000;
        pause_hidden=obs_data_get_int(settings,"hide_policy")==0;
        restart_show=obs_data_get_int(settings,"show_policy")==1;
        const bool new_random = obs_data_get_bool(settings, "random");
        const int new_order=obs_data_has_user_value(settings,"order_mode")?std::clamp(int(obs_data_get_int(settings,"order_mode")),0,2):(new_random?1:0);
        if (files == incoming && order_mode==new_order && *groups_view==*group_config && !exhausted) { ++revision; publish_locked(); return; }
        const bool retry=exhausted;
        files = std::move(incoming);order_mode=new_order;random=order_mode==1;
        groups_view=std::move(group_config);balanced.reset(group_keys,groups_view->quotas);
        files_view=std::move(view); has_files=!files.empty();
        consecutive_errors = 0; exhausted = false; last_error.clear(); ++revision;
        if(files.empty()) {
            clip_transition.store({});
            current_path.clear(); prepared_path.clear(); manual_target.clear(); history.clear();
            live_output.store({});request_locked(0,"");request_locked(1,"");timeline.reset();displayed.store({});
            publish_locked(); return;
        }
        auto found = std::find(files.begin(), files.end(), current_path);
        if (found != files.end()) {
            index = size_t(found - files.begin()); // Preserve current decoder and presentation clock.
        } else index=files.size(); // Removed current clip finishes before advancing.
        if(current_path.empty() || (retry && !decoders[active].prepared())) {
            active = 0; index = order_mode==2?balanced.peek().value_or(0):0; timeline.reset(); displayed.store({});
            live_output.store({});current_path=files[index];request_locked(active,current_path);prepared_path.clear();
        }
        if(!manual_target.empty() && std::find(files.begin(),files.end(),manual_target)==files.end()) manual_target.clear();
        if(order_mode==2&&index<files.size())balanced.presented(index);
        next=following(); prepare_locked(manual_target.empty()?files[next]:manual_target);
        publish_locked();
    }
    void tick() {
        std::unique_lock<std::mutex> lock(control, std::try_to_lock);
        if (!lock.owns_lock() || files.empty() || exhausted) return;
        const auto now = int64_t(obs_get_video_frame_time());
        if(!manual_target.empty()) {
            auto candidate=sample_locked(1-active,prepared_path,0,now);
            if(candidate.frame||(nexus::is_live(prepared_path)&&candidate.ready)) { commit_locked(std::move(candidate),now,true); return; }
            if(candidate.failed) {
                last_error=nexus::localized(nexus::Text::CouldNotOpenTheSelectedVideo); manual_target.clear(); manual_previous=false;
                prepared_path.clear(); next=following(); prepare_locked(files[next]); ++revision;
            }
            publish_locked();
        }
        if (paused || (hidden&&pause_hidden)) { timeline.pause(now); publish_locked(); return; }
        timeline.resume(now);
        if(auto transition=clip_transition.load()){
            const auto elapsed=timeline.local(now);
            transition_elapsed=elapsed;
            if(elapsed>=transition->duration)clip_transition.store({});
            if(sink)sink->frame_changed();
        }
        auto current = sample_locked(active,current_path,timeline.presentation(now),now);
        if(nexus::is_live(current_path)&&current.ready){
            timeline.start(now);
            if(live_output.load()!=live_slots[active])live_output.store(live_slots[active]);
        }
        if (current.frame) {
            timeline.start(now);
            auto prior=displayed.exchange(current.frame);
            if(sink && prior!=current.frame) sink->frame_changed();
        }
        if (current.starved) timeline.miss();
        // Inspect preparation while the current clip is still playing. A bad
        // candidate must not consume the entire look-ahead window and force us
        // to start opening its replacement only at the presentation boundary.
        auto prepared=sample_locked(1-active,prepared_path,0,now);
        live_ready=prepared.ready;
        if (prepared.failed && !prepared.ready) {
            blog(LOG_WARNING, "[Playlist NEXUS] Skipping unreadable clip: %s", files[next].c_str());
            last_error=nexus::localized(nexus::Text::SkippedFile)+prepared_path; ++revision;
            if (++consecutive_errors >= files.size()) {
                exhausted = true;
                blog(LOG_ERROR, "[Playlist NEXUS] No usable clip; playback suspended");
                publish_locked();
                return;
            }
            // Recovery walks candidates deterministically, even in random mode,
            // so the retry budget cannot be exhausted by drawing one bad file.
            if(order_mode==2){balanced.reject(next);auto candidate=balanced.peek();if(!candidate){exhausted=true;publish_locked();return;}next=*candidate;}
            else next = (next + 1) % files.size();
            prepared_path.clear(); prepare_locked(files[next]);
        }
        publish_locked();
        if (!current.ended && !current.failed) return;
        // A failed decoder may still have valid buffered frames: play these first.
        if (current.failed && current.frame && !current.ended) return;
        const auto next_time = timeline.stalled ? 0 : std::max<int64_t>(0, timeline.presentation(now) - current.end);
        auto standby=sample_locked(1-active,prepared_path,next_time,now);
        if (!standby.ready) {
            timeline.miss();
            return;
        }
        if (current.failed) timeline.miss();
        if (timeline.origin < 0) timeline.start(now);
        commit_locked(std::move(standby),now,false,current.end);
    }
    void draw_clip(gs_texture_t *image,float offset=0,float zoom=1,float opacity=1) {
        const auto width=gs_texture_get_width(image),height=gs_texture_get_height(image);
        const float scale=std::min(1920.0f/width,1080.0f/height)*zoom;
        const float left=(1920-width*scale)/2+offset,top=(1080-height*scale)/2;
        const auto first=uint32_t(std::clamp(std::ceil(-left/scale),0.0f,float(width)));
        const auto end=uint32_t(std::clamp(std::floor((1920-left)/scale),0.0f,float(width)));
        if(end<=first)return;
        auto *effect=opacity<1?fade_effect:obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if(!effect)return;
        gs_matrix_push();
        gs_matrix_translate3f(left+first*scale,top,0);
        gs_matrix_scale3f(scale,scale,1);
        gs_effect_set_texture(gs_effect_get_param_by_name(effect,"image"),image);
        if(opacity<1){
            gs_effect_set_float(gs_effect_get_param_by_name(effect,"opacity"),opacity);
            gs_blend_state_push();
            gs_enable_blending(true);
            gs_blend_function(GS_BLEND_SRCALPHA,GS_BLEND_INVSRCALPHA);
        }
        while(gs_effect_loop(effect,"Draw"))gs_draw_sprite_subregion(image,0,first,0,end-first,height);
        if(opacity<1)gs_blend_state_pop();
        gs_matrix_pop();
    }
    bool draw_live(const std::shared_ptr<nexus::LiveEntry> &entry) {
        auto source=entry->source();
        if(!source||obs_source_removed(source.get()))return false;
        const auto width=obs_source_get_width(source.get()),height=obs_source_get_height(source.get());
        if(!width||!height)return false;
        const float scale=std::min(1920.0f/width,1080.0f/height);
        rendering_live=true;
        gs_matrix_push();
        gs_matrix_translate3f((1920.0f-width*scale)/2,(1080.0f-height*scale)/2,0);
        gs_matrix_scale3f(scale,scale,1);
        obs_source_video_render(source.get());
        gs_matrix_pop();
        rendering_live=false;
        return true;
    }
    gs_texture_t *capture_live(const std::shared_ptr<nexus::LiveEntry> &entry,gs_texrender_t *&target) {
        if(!target)target=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
        if(!target)return nullptr;
        gs_texrender_reset(target);
        if(!gs_texrender_begin(target,1920,1080))return nullptr;
        struct vec4 transparent{};
        gs_clear(GS_CLEAR_COLOR,&transparent,0,0);
        gs_ortho(0,1920,0,1080,-100,100);
        gs_matrix_identity();
        gs_blend_state_push();
        gs_reset_blend_state();
        const bool drawn=draw_live(entry);
        gs_blend_state_pop();
        gs_texrender_end(target);
        return drawn?gs_texrender_get_texture(target):nullptr;
    }
    void render() {
        // Immutable frame handoff: drawing never takes the manager/control lock.
        if(!has_files.load()) {gs_texture_destroy(texture);texture=nullptr;uploaded.reset();return;}
        if(rendering_live)return;
        auto transition=clip_transition.load();
        if(!transition&&outgoing_texture){
            gs_texture_destroy(outgoing_texture);
            outgoing_texture=nullptr;
            uploaded_transition.reset();
        }
        auto live=live_output.load();
        if(live&&!transition&&draw_live(live))return;
        auto frame=displayed.load();
        if (frame && frame != uploaded) {
            if (texture && (gs_texture_get_width(texture) != uint32_t(frame->width) || gs_texture_get_height(texture) != uint32_t(frame->height))) {
                gs_texture_destroy(texture); texture = nullptr;
            }
            if (!texture) texture = gs_texture_create(frame->width, frame->height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
            if (texture) {
                gs_texture_set_image(texture, frame->rgba.data(), frame->width*4, false);
                if (!uploaded) blog(LOG_INFO, "[Playlist NEXUS] First texture uploaded: %dx%d pixel=%u,%u,%u,%u", frame->width, frame->height,
                    frame->rgba[0], frame->rgba[1], frame->rgba[2], frame->rgba[3]);
                uploaded = frame;
            }
        }
        if(transition){
            if(transition->outgoing&&uploaded_transition.lock()!=transition){
                gs_texture_destroy(outgoing_texture);
                const auto &old_frame=*transition->outgoing;
                const uint8_t *pixels=old_frame.rgba.data();
                outgoing_texture=gs_texture_create(old_frame.width,old_frame.height,GS_RGBA,1,&pixels,0);
                uploaded_transition=transition;
            }
            if(transition->kind==1&&!fade_initialized){
                fade_initialized=true;
                fade_effect=gs_effect_create(R"(
                    uniform float4x4 ViewProj;
                    uniform texture2d image;
                    uniform float opacity;
                    sampler_state linearSampler { Filter=Linear; AddressU=Clamp; AddressV=Clamp; };
                    struct Vertex { float4 pos : POSITION; float2 uv : TEXCOORD0; };
                    Vertex VS(Vertex v) { v.pos=mul(float4(v.pos.xyz,1.0),ViewProj); return v; }
                    float4 PS(Vertex v) : TARGET { float4 c=image.Sample(linearSampler,v.uv); return float4(c.rgb,c.a*opacity); }
                    technique Draw { pass { vertex_shader=VS(v); pixel_shader=PS(v); } }
                )","nexus-clip-dissolve",nullptr);
            }
            auto *old_image=transition->outgoing_live?capture_live(transition->outgoing_live,outgoing_live_target):outgoing_texture;
            auto *new_image=live?capture_live(live,incoming_live_target):texture;
            if(old_image&&new_image&&(transition->kind!=1||fade_effect)){
                const float progress=std::clamp(float(transition_elapsed.load())/transition->duration,0.0f,1.0f);
                if(progress<1){
                    draw_clip(old_image);
                    const auto motion=nexus::clip_motion(transition->kind,progress);
                    draw_clip(new_image,1920*motion.offset,motion.zoom,motion.opacity);
                    return;
                }
            }
        }
        if(live&&draw_live(live))return;
        if(!texture)return;
        const float scale = std::min(1920.0f / gs_texture_get_width(texture), 1080.0f / gs_texture_get_height(texture));
        const float w = gs_texture_get_width(texture)*scale, h = gs_texture_get_height(texture)*scale;
        gs_matrix_push(); gs_matrix_translate3f((1920-w)/2, (1080-h)/2, 0);
        auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
        while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(texture, 0, uint32_t(w), uint32_t(h));
        gs_matrix_pop();
    }
    ~Source() {
        closing=true;
        live_output.store({});live_slots={};
        for(auto id:hotkeys) obs_hotkey_unregister(id);
        for(auto &decoder:decoders) decoder.stop();
        if(sink) sink->closed();
        obs_enter_graphics(); gs_texture_destroy(texture); gs_texture_destroy(outgoing_texture); gs_effect_destroy(fade_effect); gs_texrender_destroy(outgoing_live_target); gs_texrender_destroy(incoming_live_target); obs_leave_graphics();
    }
};
const char *name(void *) { return nexus::localized(nexus::Text::ObsPlaylistNexus); }
void *create(obs_data_t *s, obs_source_t *source) {
    try { auto p = std::make_unique<Source>(source); p->configure(s); p->register_hotkeys(); return p.release(); }
    catch (...) { blog(LOG_ERROR, "[Playlist NEXUS] Source creation failed"); return nullptr; }
}
obs_properties_t *properties(void *data) {
    auto *p = obs_properties_create();
    size_t count=0;
    if(auto *source=static_cast<Source *>(data)) {
        auto *settings=obs_source_get_settings(source->owner);auto *files=obs_data_get_array(settings,"files");
        count=obs_data_array_count(files);obs_data_array_release(files);obs_data_release(settings);
    }
    const auto summary=std::to_string(count)+nexus::localized(nexus::Text::VideosLoaded);
    obs_properties_add_text(p,"count",summary.c_str(),OBS_TEXT_INFO);
    obs_properties_add_button(p,"manager",nexus::localized(nexus::Text::OpenPlaylistManager),[](obs_properties_t *,obs_property_t *,void *value) {
        if(auto *source=static_cast<Source *>(value)) nexus::open_manager(source->owner,source);
        return false;
    });
    auto *show=obs_properties_add_list(p,"show_policy",nexus::localized(nexus::Text::WhenSourceBecomesVisible),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(show,nexus::localized(nexus::Text::Continue),0); obs_property_list_add_int(show,nexus::localized(nexus::Text::RestartPlaylist),1);
    auto *hide=obs_properties_add_list(p,"hide_policy",nexus::localized(nexus::Text::WhenSourceBecomesHidden),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(hide,nexus::localized(nexus::Text::Pause2),0); obs_property_list_add_int(hide,nexus::localized(nexus::Text::Continue),1);
    using nexus::Text;
    using nexus::localized;
    auto *transition=obs_properties_add_list(p,"transition_kind",localized(Text::ClipTransition),OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    int kind=0;
    for(auto label:{Text::Cut,Text::Dissolve,Text::SlideLeft,Text::SlideRight,Text::ZoomIn})
        obs_property_list_add_int(transition,localized(label),kind++);
    auto *duration=obs_properties_add_int_slider(p,"transition_ms",localized(Text::TransitionDuration),50,2000,50);
    obs_property_int_set_suffix(duration," ms");
    return p;
}
}
bool obs_module_load(void) {
    obs_source_info info{};
    info.id = "obs_playlist_nexus";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = name; info.create = create;
    info.destroy = [](void *p) { delete static_cast<Source *>(p); };
    info.get_width = [](void *) -> uint32_t { return 1920; };
    info.get_height = [](void *) -> uint32_t { return 1080; };
    info.get_properties = properties;
    info.get_defaults=[](obs_data_t *settings){obs_data_set_default_int(settings,"transition_ms",400);};
    info.update = [](void *p, obs_data_t *s) {
        auto *v = static_cast<Source *>(p); v->configure(s);
    };
    info.show = [](void *p) { auto *v = static_cast<Source *>(p); std::lock_guard<std::mutex> lock(v->control); v->hidden = false; if(v->restart_show&&!v->files.empty()) v->jump_locked(v->files[0]); v->publish_locked(); };
    info.hide = [](void *p) { auto *v = static_cast<Source *>(p); std::lock_guard<std::mutex> lock(v->control); v->hidden = true; v->publish_locked(); };
    info.video_tick = [](void *p, float) { static_cast<Source *>(p)->tick(); };
    info.video_render = [](void *p, gs_effect_t *) { static_cast<Source *>(p)->render(); };
    obs_register_source(&info);
    return true;
}
void obs_module_unload(void) { nexus::shutdown_managers(); }
