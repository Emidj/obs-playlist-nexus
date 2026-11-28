#include <obs.h>
#include <obs-hotkey.h>
#include <util/base.h>
#include "continuity_probe.hpp"
#include <QApplication>
#include "grouping.hpp"
#include <QComboBox>
#include <QDialog>
#include <QInputDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPointer>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <thread>
namespace {
ContinuityProbe oracle;
std::atomic<bool> inspect{true};
std::atomic<unsigned> transitions{0},failures{0};
std::atomic<int> clip{0},original{0};
std::atomic<uint64_t> image_hash{0};
std::atomic<bool> live_pixel{false};
void require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
void hotkey(const char *name) {
    struct Match {const char *name;obs_hotkey_id id=OBS_INVALID_HOTKEY_ID;} match{name};
    obs_enum_hotkeys([](void *data,obs_hotkey_id id,obs_hotkey_t *key){auto *m=static_cast<Match *>(data);if(strcmp(obs_hotkey_get_name(key),m->name)==0)m->id=id;return true;},&match);
    require(match.id!=OBS_INVALID_HOTKEY_ID,"hotkey registration absent");
    obs_hotkey_enable_callback_rerouting(true);
    obs_hotkey_trigger_routed_callback(match.id,true);obs_hotkey_trigger_routed_callback(match.id,false);
}
void logger(int level,const char *format,va_list args,void *) {
    char message[4096];vsnprintf(message,sizeof(message),format,args);
    unsigned long long n=0,bad=0;
    if(sscanf(message,"[Playlist NEXUS] transition=%llu continuity_failures=%llu",&n,&bad)==2){transitions=unsigned(n);failures=unsigned(bad);}
    if(level<=LOG_WARNING)std::cerr<<message<<'\n';
    if(strstr(message,"Native preview")) std::cout<<message<<std::endl;
}
void capture(void *,video_data *frame) {
    const auto *pixel=frame->data[0]+540*frame->linesize[0]+960*4;
    live_pixel=pixel[0]>245&&pixel[1]<8&&pixel[2]>245;
    if(inspect) {oracle.frame(frame->data[0],frame->linesize[0],int64_t(frame->timestamp));clip=oracle.last_clip;original=oracle.last_frame;}
    uint64_t hash=1469598103934665603ull;
    for(int y=30;y<1080;y+=60)for(int x=30;x<1920;x+=60) {
        auto *p=frame->data[0]+y*frame->linesize[0]+x*4;
        for(int c=0;c<3;++c)hash=(hash^p[c])*1099511628211ull;
    }
    image_hash=hash;
}
bool spin(const std::function<bool()> &done,int timeout=5000) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
    while(std::chrono::steady_clock::now()<end) {
        QApplication::processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        if(done())return true;std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    return done();
}
void pump(int milliseconds) {spin([]{return false;},milliseconds);}
QWidget *manager() {
    for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()=="nexusPlaylistManager"&&widget->isVisible())return widget;
    return nullptr;
}
void open(obs_source_t *source) {
    auto *properties=obs_source_properties(source);auto *button=obs_properties_get(properties,"manager");
    require(button,"manager property absent");obs_property_button_clicked(button,source);obs_properties_destroy(properties);
    require(spin([]{return manager()!=nullptr;}),"manager did not open");
}
template<typename T>T *control(const char *name) {auto *window=manager();require(window,"manager disappeared");auto *value=window->findChild<T *>(name);require(value,"UI control missing");return value;}
bool settings_paused(obs_source_t *source) {auto *settings=obs_source_get_settings(source);bool value=obs_data_get_bool(settings,"paused");obs_data_release(settings);return value;}
void files(obs_source_t *source,const std::vector<std::string> &paths) {
    auto *settings=obs_source_get_settings(source);auto *array=obs_data_array_create();
    for(auto &path:paths){auto *item=obs_data_create();obs_data_set_string(item,"value",path.c_str());obs_data_array_push_back(array,item);obs_data_release(item);}
    obs_data_set_array(settings,"files",array);obs_data_array_release(array);obs_data_set_bool(settings,"random",false);obs_source_update(source,settings);obs_data_release(settings);
}
void drop(const QStringList &paths) {
    QMimeData mime;QList<QUrl> urls;for(auto &path:paths)urls.push_back(QUrl::fromLocalFile(path));mime.setUrls(urls);
    auto *list=control<QListWidget>("playlistList");
    QDragEnterEvent enter(QPoint(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(list->viewport(),&enter);
    QDropEvent event(QPointF(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(list->viewport(),&event);
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(argc<7){std::cerr<<"usage: manager_integration_tests obs-bin plugin.dll manifest real-list folder output-prefix\n";return 2;}
    bool started=false;obs_source_t *source=nullptr;
    try {
        base_set_log_handler(logger,nullptr);
        oracle.load(argv[3],std::string(argv[6])+"-continuity.csv");
        const auto bin=std::filesystem::path(argv[1]);
        obs_add_data_path(((bin/"../../data/libobs").generic_string()+"/").c_str());
        require(obs_startup("es-ES",nullptr,nullptr),"obs_startup failed");started=true;
        std::cout<<"libobs="<<obs_get_version_string()<<" Qt="<<qVersion()<<" platform="<<QGuiApplication::platformName().toStdString()<<std::endl;
        obs_video_info video{};auto graphics=(bin/"libobs-d3d11.dll").string();video.graphics_module=graphics.c_str();
        video.fps_num=60;video.fps_den=1;video.base_width=video.output_width=1920;video.base_height=video.output_height=1080;
        video.output_format=VIDEO_FORMAT_RGBA;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_FULL;video.scale_type=OBS_SCALE_BILINEAR;
        require(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"video init failed");
        obs_module_t *module=nullptr;require(obs_open_module(&module,argv[2],".")==MODULE_SUCCESS&&obs_init_module(module),"module load failed");
        auto *settings=obs_data_create();source=obs_source_create("obs_playlist_nexus","Manager integration",settings,nullptr);obs_data_release(settings);require(source,"source create failed");
        std::vector<std::string> marker_paths;for(auto &fixture:oracle.fixtures)marker_paths.push_back(fixture.path);files(source,marker_paths);
        obs_set_output_source(0,source);
        video_scale_info conversion{};conversion.format=VIDEO_FORMAT_RGBA;conversion.width=1920;conversion.height=1080;conversion.range=VIDEO_RANGE_FULL;conversion.colorspace=VIDEO_CS_709;
        obs_add_raw_video_callback(&conversion,capture,nullptr);
        require(spin([]{return transitions>=2;}),"playback did not start");
        struct LocaleCase {const char *locale;const char *button;const char *order;};
        for(const LocaleCase locale: {LocaleCase{"en-US","Open Playlist Manager","Sequential"},
                {"es-ES","Abrir Playlist Manager","Secuencial"},{"fr-FR","Ouvrir le gestionnaire de listes","Séquentiel"},
                {"de-DE","Wiedergabelisten-Verwaltung öffnen","Der Reihe nach"},{"it-IT","Apri gestore playlist","Sequenziale"},
                {"pt-BR","Open Playlist Manager","Sequential"}}){
            obs_set_locale(locale.locale);
            auto *translated=obs_source_properties(source);
            const auto label=QString::fromUtf8(obs_property_description(obs_properties_get(translated,"manager")));
            obs_properties_destroy(translated);
            require(label==QString::fromUtf8(locale.button),"localized OBS properties mismatch");
            open(source);
            require(control<QComboBox>("orderCombo")->itemText(0)==QString::fromUtf8(locale.order),"localized manager mismatch");
            manager()->close();
            require(spin([]{return manager()==nullptr;}),"localized manager did not close");
        }
        obs_set_locale("es-ES");
        std::cout<<"PASS: five plugin languages and English fallback"<<std::endl;
        open(source);auto *first=manager();open(source);require(manager()==first,"duplicate manager created");
        pump(700);
        require(!control<QLabel>("preloadStatus")->text().isEmpty(),"status absent");
        auto *list=control<QListWidget>("playlistList");list->setCurrentRow(3);control<QPushButton>("moveUpButton")->click();
        require(spin([]{return transitions>=15;}),"playlist reorder interrupted engine");
        // Remove the current item after it has presented some original frames.
        require(spin([]{return original>=2;}),"no mid-clip frame available");
        const auto actual=oracle.find(clip.load())->path;
        for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toString()==QString::fromUtf8(actual.c_str()))list->setCurrentRow(i);
        control<QPushButton>("removeButton")->click();
        for(int i=0;i<3;++i){pump(150);manager()->close();pump(50);open(source);}
        require(spin([]{return transitions>=100;},40000),"continuity phase timed out");
        inspect=false;
        // Synchronize with the capture thread before inspecting its oracle.
        obs_remove_raw_video_callback(capture,nullptr);
        require(oracle.passed(95)&&failures==0,"opening/editing/closing manager broke frame continuity");
        std::cout<<"continuity_transitions="<<transitions<<" identity_frames="<<oracle.valid<<" timing_errors="<<oracle.timing_errors<<" gaps="<<oracle.timestamp_gaps<<" excess_repeats="<<oracle.excess_repeats<<std::endl;
        obs_add_raw_video_callback(&conversion,capture,nullptr);
        std::ifstream real_input(argv[4]);std::vector<std::string> real;std::string line;
        while(std::getline(real_input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty())real.push_back(line);}require(real.size()>=3,"real clips absent");
        files(source,real);
        const auto name=[&](int index){return QFileInfo(QString::fromUtf8(real[index].c_str())).fileName();};
        require(spin([&]{return control<QLabel>("currentFile")->text()==name(0);}),"current clip not retained/advanced after replacement");
        hotkey("nexus_playlist.pause");pump(150);require(settings_paused(source),"pause not persisted");
        const auto frozen=image_hash.load();pump(170);require(image_hash==frozen,"pause did not freeze output");
        hotkey("nexus_playlist.next");require(spin([&]{return control<QLabel>("currentFile")->text()==name(1);}),"Next failed");
        require(settings_paused(source),"Next unexpectedly resumed playback");pump(100);require(image_hash!=frozen,"Next did not present prepared image while paused");
        hotkey("nexus_playlist.previous");require(spin([&]{return control<QLabel>("currentFile")->text()==name(0);}),"Previous history failed");
        list=control<QListWidget>("playlistList");list->itemDoubleClicked(list->item(2));
        require(spin([&]{return control<QLabel>("currentFile")->text()==name(2);}),"double-click jump failed");
        pump(150);const auto before_import=image_hash.load();
        drop({QString::fromUtf8(argv[5])});require(spin([&]{return control<QListWidget>("playlistList")->count()==265;},15000),"folder import/deduplication failed");
        require(control<QLabel>("currentFile")->text()==name(2)&&settings_paused(source)&&image_hash==before_import,"folder import interrupted paused clip");
        auto *properties=obs_source_properties(source);require(QString::fromUtf8(obs_property_description(obs_properties_get(properties,"count"))).contains("265"),"source property count is stale");obs_properties_destroy(properties);
        drop({QString::fromUtf8(real[0].c_str()),QString::fromUtf8(real[0].c_str())});pump(100);require(control<QListWidget>("playlistList")->count()==265,"duplicate paths imported");
        control<QComboBox>("orderCombo")->setCurrentIndex(1);pump(50);
        auto *saved=obs_source_get_settings(source);require(obs_data_get_bool(saved,"random"),"order not saved");obs_data_release(saved);
        control<QComboBox>("orderCombo")->setCurrentIndex(0);pump(50);
        auto *preview=control<QWidget>("nativePreview");
        {
            require(spin([]{return control<QLabel>("preloadStatus")->text().contains("READY");}),"readiness event did not reach the manager");
            auto shot=preview->grab().toImage();require(!shot.isNull(),"preview capture failed");
            auto pixel=shot.pixelColor(shot.width()/2,shot.height()/2);
            require(pixel.red()+pixel.green()+pixel.blue()>20,"native preview is black");
            unsigned minimum=765,maximum=0;
            for(int y=shot.height()/5;y<shot.height()*4/5;y+=std::max(1,shot.height()/10))
                for(int x=shot.width()/5;x<shot.width()*4/5;x+=std::max(1,shot.width()/10)) {
                    auto color=shot.pixelColor(x,y);unsigned sum=color.red()+color.green()+color.blue();minimum=std::min(minimum,sum);maximum=std::max(maximum,sum);
                }
            require(maximum-minimum>40,"native preview is a uniform surface, not the clip");
            std::cout<<"preview_pixel_variation="<<maximum-minimum<<std::endl;
            if(argc>7) {
                QImage reference(QString::fromUtf8(argv[7]));require(!reference.isNull(),"reference image missing");
                QImage expected(shot.size(),QImage::Format_RGBA8888);expected.fill(QColor("#090b12"));
                const auto canvas=QSizeF(1920,1080).scaled(shot.size(),Qt::KeepAspectRatio);
                const auto fitted=QSizeF(reference.size()).scaled(canvas,Qt::KeepAspectRatio);
                {QPainter painter(&expected);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(QRectF((shot.width()-fitted.width())/2,(shot.height()-fitted.height())/2,fitted.width(),fitted.height()),reference);}
                double error=0;unsigned samples=0;
                for(int y=8;y<shot.height();y+=11)for(int x=8;x<shot.width();x+=11) {
                    auto a=shot.pixelColor(x,y),b=expected.pixelColor(x,y);
                    error+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());samples+=3;
                }
                std::cout<<"preview_reference_mae="<<error/samples<<std::endl;
                require(error/samples<18,"preview does not match the actual clip frame");
            }
            require(manager()->grab().save(QString::fromUtf8(argv[6])+"-manager.png"),"manager screenshot failed");
        }
        // Group preview is transactional; no setting changes on Cancel.
        auto grouping=[&](int mode,bool accept){
            QTimer::singleShot(0,qApp,[=]{
                for(auto *widget:QApplication::topLevelWidgets())if(widget->objectName()=="groupingDialog"){
                    auto *dialog=static_cast<QDialog *>(widget);auto *combo=dialog->findChild<QComboBox *>("groupingMode");combo->setCurrentIndex(mode);
                    auto *tree=dialog->findChild<QTreeWidget *>("groupingPreview");require(tree&&tree->topLevelItemCount()>0,"group preview empty");
                    if(accept)dialog->accept();else dialog->reject();return;
                }
                throw std::runtime_error("grouping dialog absent");
            });
            control<QPushButton>("groupingButton")->click();pump(100);
        };
        require(nexus::groupLabel("C:/clips/Aerial_01.mp4",1,"_")=="Aerial","prefix grouping failed");
        require(nexus::groupLabel("C:/clips/Back-01.mp4",1,"-")=="Back","dash grouping failed");
        require(nexus::groupLabel("C:/clips/People 01.mp4",1," ")=="People","space grouping failed");
        require(nexus::groupLabel("C:/clips/Unique.mov",1,"_")=="Unique","distinct filename grouping failed");
        require(nexus::groupLabel("C:/clips/Aerial/video.mov",2,"_")=="Aerial","folder grouping failed");
        grouping(1,false);
        saved=obs_source_get_settings(source);require(obs_data_get_int(saved,"group_by")==0,"cancel changed grouping");obs_data_release(saved);
        grouping(1,true);require(control<QLabel>("groupSummary")->text().contains("1 grupos"),"prefix preview/group count mismatch");
        list=control<QListWidget>("playlistList");list->clearSelection();list->item(0)->setSelected(true);list->item(1)->setSelected(true);
        QTimer::singleShot(0,qApp,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QInputDialog *>(widget)){dialog->setTextValue("Featured");dialog->accept();return;}});
        control<QPushButton>("assignGroupButton")->click();pump(100);
        require(control<QLabel>("groupSummary")->text().contains("2 grupos"),"manual grouping lost other detected groups");
        control<QComboBox>("orderCombo")->setCurrentIndex(2);pump(100);
        saved=obs_source_get_settings(source);require(obs_data_get_int(saved,"order_mode")==2&&obs_data_get_int(saved,"group_by")==3,"balanced/manual settings not saved");obs_data_release(saved);
        const auto before_group_next=control<QLabel>("currentFile")->text();hotkey("nexus_playlist.next");
        require(spin([&]{return control<QLabel>("currentFile")->text()!=before_group_next;}),"balanced Next failed");
        require(settings_paused(source),"balanced navigation resumed paused source");
        hotkey("nexus_playlist.previous");require(spin([&]{return control<QLabel>("currentFile")->text()==before_group_next;}),"balanced Previous history failed");
        require(manager()->grab().save(QString::fromUtf8(argv[6])+"-groups.png"),"group manager screenshot failed");
        hotkey("nexus_playlist.play");pump(200);require(!settings_paused(source),"Play not saved");
        const auto moving=image_hash.load();manager()->close();pump(180);require(image_hash!=moving,"closing manager paused Program output");
        open(source);require(control<QListWidget>("playlistList")->count()==265,"list not restored on reopen");
        saved=obs_source_get_settings(source);auto json=std::string(obs_data_get_json(saved));obs_data_release(saved);
        auto *roundtrip=obs_data_create_from_json(json.c_str());auto *clone=obs_source_create("obs_playlist_nexus","Roundtrip",roundtrip,nullptr);obs_data_release(roundtrip);require(clone,"settings roundtrip failed");
        auto *clone_settings=obs_source_get_settings(clone);auto *clone_files=obs_data_get_array(clone_settings,"files");require(obs_data_array_count(clone_files)==265,"saved list changed");
        require(obs_data_get_int(clone_settings,"order_mode")==2&&obs_data_get_int(clone_settings,"group_by")==3,"group/order roundtrip failed");
        auto *first_group=obs_data_array_item(clone_files,0);require(std::string(obs_data_get_string(first_group,"manual_group"))=="Featured","manual group roundtrip failed");obs_data_release(first_group);
        obs_data_array_release(clone_files);obs_data_release(clone_settings);obs_source_release(clone);
        obs_source_info test_camera{};
        test_camera.id="nexus_test_camera";test_camera.type=OBS_SOURCE_TYPE_INPUT;test_camera.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
        test_camera.get_name=[](void *){return "Test camera";};
        test_camera.create=[](obs_data_t *,obs_source_t *self)->void *{return self;};
        test_camera.destroy=[](void *){};
        test_camera.get_width=test_camera.get_height=[](void *)->uint32_t{return 1920;};
        test_camera.video_render=[](void *,gs_effect_t *){
            auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);
            vec4 magenta;vec4_set(&magenta,1,0,1,1);
            gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&magenta);
            while(gs_effect_loop(effect,"Solid"))gs_draw_sprite(nullptr,0,1920,1080);
        };
        obs_register_source(&test_camera);
        auto *camera=obs_source_create("nexus_test_camera","Camera fixture",nullptr,nullptr);
        require(camera,"test camera creation failed");
        auto *cab=obs_scene_create("Cabin fixture");obs_scene_add(cab,camera);
        auto *recursive=obs_scene_create("Recursive fixture");auto *recursive_item=obs_scene_add(recursive,source);
        auto *group=obs_scene_add_group(recursive,"Camera group fixture");
        obs_sceneitem_group_add_item(group,obs_scene_add(recursive,camera));
        if(qEnvironmentVariableIsSet("NEXUS_CAPTURE_ENGLISH")){
            const auto prefix=QString::fromUtf8(argv[6]);
            manager()->close();
            require(spin([]{return manager()==nullptr;}),"manager did not close");
            obs_set_locale("en-US");
            open(source);
            require(manager()->grab().save(prefix+"-en-manager.png"),"English manager capture failed");
            for(const auto &window: {std::pair<const char *,const char *>{"groupingButton","-en-groups.png"},
                    {"addLiveButton","-en-source.png"}}){
                bool captured=false;
                QTimer::singleShot(0,qApp,[&]{
                    if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())){
                        captured=dialog->grab().save(prefix+window.second);
                        dialog->reject();
                    }
                });
                control<QPushButton>(window.first)->click();
                require(captured,"English dialog capture failed");
            }
            manager()->close();
            require(spin([]{return manager()==nullptr;}),"English manager did not close");
            obs_set_locale("es-ES");
            open(source);
        }
        files(source,{real[0]});pump(100);
        auto *transition_settings=obs_source_get_settings(source);
        obs_data_set_int(transition_settings,"transition_kind",1);
        obs_data_set_int(transition_settings,"transition_ms",400);
        obs_source_update(source,transition_settings);
        obs_data_release(transition_settings);
        bool picker_ok=false;
        QTimer::singleShot(0,qApp,[&]{
            for(auto *widget:QApplication::topLevelWidgets())if(auto *picker=widget->findChild<QComboBox *>("liveSourceCombo")){
                auto *dialog=qobject_cast<QDialog *>(widget);
                auto *type=dialog->findChild<QComboBox *>("liveTypeCombo");
                auto *groups=dialog->findChild<QComboBox *>("liveGroupCombo");
                const auto label=QStringLiteral("Recursive fixture / Camera group fixture");
                const int cabin=groups->findText(label);
                type->setCurrentIndex(1);
                picker_ok=cabin>=0&&picker->findText(label)<0&&picker->findText("Cabin fixture")>=0&&picker->findText("Recursive fixture")<0&&groups->isVisible()&&!picker->isVisible();
                if(!picker_ok){dialog->reject();return;}
                groups->setCurrentIndex(cabin);dialog->findChild<QSpinBox *>()->setValue(1);dialog->accept();return;
            }
        });
        control<QPushButton>("addLiveButton")->click();
        require(picker_ok,"live picker did not show camera/scene or excluded recursion incorrectly");
        require(spin([]{return live_pixel.load();},9000),"balanced rotation never rendered the camera scene");
        require(spin([]{return !live_pixel.load();},3000),"rotation did not leave the camera scene after its duration");
        control<QComboBox>("orderCombo")->setCurrentIndex(0);
        require(spin([]{return live_pixel.load();},9000),"sequential rotation never rendered the scene");
        require(spin([]{return !live_pixel.load();},3000),"sequential rotation stayed on the scene");
        obs_sceneitem_remove(recursive_item);
        obs_sceneitem_remove(group);
        obs_scene_release(recursive);obs_scene_release(cab);obs_source_release(camera);
        std::cout<<"PASS: native camera and scene picker, recursive scene rejected, live frames in balanced and sequential rotation"<<std::endl;
        obs_remove_raw_video_callback(capture,nullptr);obs_set_output_source(0,nullptr);obs_source_release(source);source=nullptr;
        require(spin([]{return manager()==nullptr;}),"manager survived source destruction");
        settings=obs_data_create();source=obs_source_create("obs_playlist_nexus","Import cancellation",settings,nullptr);obs_data_release(settings);
        open(source);drop({QString::fromUtf8(argv[5])});manager()->close();pump(100);obs_source_release(source);source=nullptr;
        settings=obs_data_create();source=obs_source_create("obs_playlist_nexus","Group dialog destruction",settings,nullptr);obs_data_release(settings);open(source);
        QTimer::singleShot(20,qApp,[&]{obs_source_release(source);source=nullptr;});
        control<QPushButton>("groupingButton")->click();require(spin([]{return manager()==nullptr;}),"group editor survived source destruction");
        obs_shutdown();started=false;
        std::cout<<"PASS: live preview, continuity, imports, hotkeys, grouping preview/apply/cancel, manual assignment, balanced Next/Previous, group/order roundtrip and source destruction\n";
    } catch(const std::exception &error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        if(started){obs_remove_raw_video_callback(capture,nullptr);obs_set_output_source(0,nullptr);if(source)obs_source_release(source);obs_shutdown();}
        return 1;
    }
}
