// Test instrumentation for project/work portable OBS only. Never ship this module.
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <stdexcept>
#include <string>

OBS_DECLARE_MODULE()
namespace {
QTimer *watchdog=nullptr;
QString phase, report;
obs_source_t *source=nullptr;
bool completed=false;
QImage initialPreview;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void finish(bool ok,const char *message) {
    if(completed)return;
    completed=true;
    QFile file(report);
    if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(QJsonObject{{"passed",ok},{"phase",phase},{"message",message}}).toJson());
    blog(ok?LOG_INFO:LOG_ERROR,"[NEXUS FRONTEND TEST] %s: %s",ok?"PASS":"FAIL",message);
    if(source){obs_source_release(source);source=nullptr;}
    QTimer::singleShot(300,qApp,[]{
        for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()=="nexusPlaylistManager")widget->close();
        auto *main=static_cast<QWidget *>(obs_frontend_get_main_window());if(main)main->close();
    });
}
QWidget *manager(){for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="nexusPlaylistManager")return w;return nullptr;}
void verify() {
    try {
        auto *window=manager();require(window,"manager did not open in actual frontend");
        auto *list=window->findChild<QListWidget *>("playlistList");require(list&&list->count()==2,"playlist not restored");
        auto *order=window->findChild<QComboBox *>("orderCombo");require(order&&order->currentIndex()==2,"balanced setting not restored");
        auto *settings=obs_source_get_settings(source);
        bool paused=obs_data_get_bool(settings,"paused");bool grouping=obs_data_get_int(settings,"group_by")==3;
        auto *files=obs_data_get_array(settings,"files");bool count=obs_data_array_count(files)==2;
        obs_data_array_release(files);obs_data_release(settings);
        require(paused&&count&&grouping,"source settings not persisted");
        auto *groupSettings=obs_source_get_settings(source);auto *groupFiles=obs_data_get_array(groupSettings,"files");
        bool groupsSaved=true;for(size_t i=0;i<obs_data_array_count(groupFiles);++i){auto *item=obs_data_array_item(groupFiles,i);groupsSaved=groupsSaved&&std::string(obs_data_get_string(item,"manual_group"))=="Group_"+std::to_string(i);obs_data_release(item);}
        obs_data_array_release(groupFiles);obs_data_release(groupSettings);require(groupsSaved,"manual groups not persisted after restart");
        auto *preview=window->findChild<QWidget *>("nativePreview");require(preview,"preview absent");
        initialPreview=preview->grab().toImage().convertToFormat(QImage::Format_RGB32);
        auto *play=window->findChild<QPushButton *>("playButton");require(play,"play button absent");play->click();
        QTimer::singleShot(1300,qApp,[]{try {
            auto *window=manager();require(window,"manager disappeared during playback");
            auto *pause=window->findChild<QPushButton *>("pauseButton");require(pause,"pause button absent");pause->click();
            auto image=window->findChild<QWidget *>("nativePreview")->grab().toImage().convertToFormat(QImage::Format_RGB32);
            require(image.size()==initialPreview.size(),"preview dimensions changed");
            size_t changed=0,total=0;
            for(int y=0;y<image.height();y+=4)for(int x=0;x<image.width();x+=4){++total;if(image.pixel(x,y)!=initialPreview.pixel(x,y))++changed;}
            require(changed>total/10,"playback did not update preview");
            auto *saved=obs_source_get_settings(source);bool paused=obs_data_get_bool(saved,"paused");obs_data_release(saved);require(paused,"pause control failed");
            window->grab().save(report+".png");obs_frontend_save();
            finish(true,phase=="create"?"scene created; manager playback and pause checked; save requested":"scene restored after OBS restart; manager playback and pause checked");
        }catch(const std::exception &e){finish(false,e.what());}});
    }catch(const std::exception &e){finish(false,e.what());}
}
void run() {
    try {
        if(phase=="create") {
            auto *settings=obs_data_create();auto *files=obs_data_array_create();
            for(const auto &path:qEnvironmentVariable("NEXUS_FRONTEND_CLIPS").split('|')){
                auto *item=obs_data_create();obs_data_set_string(item,"value",path.toUtf8().constData());auto label="Group_"+std::to_string(obs_data_array_count(files));obs_data_set_string(item,"manual_group",label.c_str());obs_data_array_push_back(files,item);obs_data_release(item);
            }
            obs_data_set_array(settings,"files",files);obs_data_array_release(files);
            obs_data_set_bool(settings,"paused",true);obs_data_set_bool(settings,"random",true);
            obs_data_set_int(settings,"order_mode",2);obs_data_set_int(settings,"group_by",3);
            source=obs_source_create("obs_playlist_nexus","NEXUS persistence test",settings,nullptr);obs_data_release(settings);require(source,"source creation failed");
            auto *scene=obs_scene_create("NEXUS persistence scene");require(scene,"scene creation failed");
            obs_scene_add(scene,source);obs_frontend_set_current_scene(obs_scene_get_source(scene));obs_scene_release(scene);
        }else{
            source=obs_get_source_by_name("NEXUS persistence test");require(source,"saved source absent after restart");
            auto *current=obs_frontend_get_current_scene();bool match=current&&std::string(obs_source_get_name(current))=="NEXUS persistence scene";
            obs_source_release(current);require(match,"saved scene not active after restart");
        }
        auto *props=obs_source_properties(source);auto *button=obs_properties_get(props,"manager");
        require(button,"manager property absent");obs_property_button_clicked(button,source);obs_properties_destroy(props);
        QTimer::singleShot(1500,qApp,verify);
    }catch(const std::exception &e){finish(false,e.what());}
}
void event(enum obs_frontend_event value,void *) {
    if(value==OBS_FRONTEND_EVENT_FINISHED_LOADING)QTimer::singleShot(1000,qApp,run);
}
}
bool obs_module_load() {
    phase=qEnvironmentVariable("NEXUS_FRONTEND_PHASE");report=qEnvironmentVariable("NEXUS_FRONTEND_REPORT");
    if(report.isEmpty()||(phase!="create"&&phase!="reload"))return false;
    watchdog=new QTimer(qApp);watchdog->setSingleShot(true);
    QObject::connect(watchdog,&QTimer::timeout,[]{finish(false,"frontend timed out before completion");});watchdog->start(35000);
    obs_frontend_add_event_callback(event,nullptr);return true;
}
void obs_module_unload(){obs_frontend_remove_event_callback(event,nullptr);delete watchdog;watchdog=nullptr;if(source){obs_source_release(source);source=nullptr;}}
