#include "manager.hpp"
#include "decoder.hpp"
#include "grouping.hpp"
#include "localization.hpp"
#include "live_entry.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QCollator>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollArea>
#include <QFormLayout>
#include <QThread>
#include <QThreadPool>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <optional>
#include <functional>
#include <map>
#include <mutex>

namespace nexus {
namespace {
class ManagerWindow;
std::map<obs_source_t *,QPointer<ManagerWindow>> windows;
QThreadPool *imports=nullptr;
QString text(const std::string &value) { return QString::fromUtf8(value.data(),int(value.size())); }
bool livePath(const QString &path){return is_live(path.toUtf8().toStdString());}
struct ScenePicker {
    QComboBox *combo;
    QComboBox *groups;
    QString location;
    obs_source_t *playlist;
    static bool addItem(obs_scene_t *,obs_sceneitem_t *item,void *data){
        const auto &parent=*static_cast<ScenePicker *>(data);
        auto *source=obs_sceneitem_get_source(item);
        if(!source||source==parent.playlist)return true;
        const auto name=QString::fromUtf8(obs_source_get_name(source));
        const auto location=parent.location+QStringLiteral(" / ")+name;
        const bool isGroup=obs_sceneitem_is_group(item);
        if(usable_live_source(source)){
            auto *target=isGroup?parent.groups:parent.combo;
            const auto id=QString::fromUtf8(obs_source_get_uuid(source));
            if(target->findData(id)<0)target->addItem(location,id);
        }
        if(isGroup){
            ScenePicker child{parent.combo,parent.groups,location,parent.playlist};
            obs_sceneitem_group_enum_items(item,addItem,&child);
        }
        return true;
    }
};
QString entryName(const QString &path){
    if(!livePath(path))return QFileInfo(path).fileName();
    auto source=resolve_live(path.toUtf8().toStdString());
    return source?QString::fromUtf8(obs_source_get_name(source.get())):ui(Text::ObsSourceUnavailable);
}
QString duration(int64_t ns) {
    if(ns<=0) return QStringLiteral("—");
    if(ns<1000000000) return QString::number(ns/1e9,'f',2)+" s";
    const auto seconds=(ns+500000000)/1000000000;
    return QStringLiteral("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0'));
}
bool supported(const QFileInfo &info) {
    static const QSet<QString> extensions={"mp4","mkv","mov","webm"};
    return info.isFile()&&extensions.contains(info.suffix().toLower());
}
struct Channel final:ManagerSink,std::enable_shared_from_this<Channel> {
    std::mutex gate;
    QObject *receiver=nullptr;
    bool pending=false;
    bool frame_pending=false;
    std::atomic<bool> frame_enabled{false};
    std::atomic<int64_t> last_frame{0};
    std::function<void()> refresh,close,frame_refresh;
    std::function<void(QStringList)> imported;
    void changed() override {
        std::lock_guard<std::mutex> lock(gate);
        if(!receiver||pending||!frame_enabled) return;
        pending=true;
        QMetaObject::invokeMethod(receiver,[self=shared_from_this()] {
            std::function<void()> call;
            {std::lock_guard<std::mutex> guard(self->gate); self->pending=false; if(self->receiver) call=self->refresh;}
            if(call) call();
        },Qt::QueuedConnection);
    }
    void closed() override {
        std::lock_guard<std::mutex> lock(gate); if(!receiver) return;
        QMetaObject::invokeMethod(receiver,[self=shared_from_this()] {
            std::function<void()> call;
            {std::lock_guard<std::mutex> guard(self->gate); if(self->receiver) call=self->close;}
            if(call) call();
        },Qt::QueuedConnection);
    }
    void frame_changed() override {
        if(!frame_enabled) return;
        const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        auto previous=last_frame.load();
        if(now-previous<66666667 || !last_frame.compare_exchange_strong(previous,now)) return;
        std::lock_guard<std::mutex> lock(gate);if(!receiver||frame_pending)return;frame_pending=true;
        QMetaObject::invokeMethod(receiver,[self=shared_from_this()] {
            std::function<void()> call;
            {std::lock_guard<std::mutex> guard(self->gate);self->frame_pending=false;if(self->receiver)call=self->frame_refresh;}
            if(call)call();
        },Qt::QueuedConnection);
    }
    void files(QStringList paths) {
        std::lock_guard<std::mutex> lock(gate); if(!receiver) return;
        QMetaObject::invokeMethod(receiver,[self=shared_from_this(),paths=std::move(paths)] {
            std::function<void(QStringList)> call;
            {std::lock_guard<std::mutex> guard(self->gate); if(self->receiver) call=self->imported;}
            if(call) call(paths);
        },Qt::QueuedConnection);
    }
    void detach() {
        std::lock_guard<std::mutex> lock(gate); receiver=nullptr; frame_enabled=false; refresh={}; close={};frame_refresh={}; imported={};
    }
};
class PlaylistList final:public QListWidget {
public:
    std::function<void()> reordered;
    std::function<void(QStringList)> dropped;
    explicit PlaylistList(QWidget *parent):QListWidget(parent) {
        setDragDropMode(QAbstractItemView::InternalMove); setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::ExtendedSelection); setAcceptDrops(true);
        setObjectName("playlistList");
    }
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(event->mimeData()->hasUrls()) event->acceptProposedAction(); else QListWidget::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if(event->mimeData()->hasUrls()) event->acceptProposedAction(); else QListWidget::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent *event) override {
        if(event->mimeData()->hasUrls()) {
            QStringList paths;
            for(const auto &url:event->mimeData()->urls()) if(url.isLocalFile()) paths.push_back(url.toLocalFile());
            if(dropped) dropped(paths); event->acceptProposedAction();
        } else {QListWidget::dropEvent(event); if(reordered) reordered();}
    }
};
class Preview final:public QWidget {
    obs_weak_source_t *weak=nullptr;
    ManagerBackend *backend=nullptr;
    std::shared_ptr<Channel> channel;
    PreviewFrame frame;
    bool showing=false;
    bool live=false;
    void release() {
        channel->frame_enabled=false;
        if(showing) {
            if(auto *source=obs_weak_source_get_source(weak)) {obs_source_dec_showing(source); obs_source_release(source);}
            showing=false;
        }
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.fillRect(rect(),QColor("#090b12"));
        if(live||!frame.incoming){painter.setPen(QColor("#7b8095"));painter.drawText(rect(),Qt::AlignCenter,live?ui(Text::LiveSourceVisibleInObs):ui(Text::PreparingVideo));return;}
        const QSizeF canvas=QSizeF(1920,1080).scaled(size(),Qt::KeepAspectRatio);
        painter.setClipRect(QRectF((width()-canvas.width())/2,(height()-canvas.height())/2,canvas.width(),canvas.height()));
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        auto draw=[&](const Frame &clip,ClipMotion motion){
            const QImage image(clip.rgba.data(),clip.width,clip.height,clip.width*4,QImage::Format_RGBA8888);
            const QSizeF fitted=QSizeF(clip.width,clip.height).scaled(canvas,Qt::KeepAspectRatio)*motion.zoom;
            const QRectF target((width()-fitted.width())/2+canvas.width()*motion.offset,
                (height()-fitted.height())/2,fitted.width(),fitted.height());
            painter.setOpacity(motion.opacity);
            painter.drawImage(target,image);
        };
        if(frame.outgoing)draw(*frame.outgoing,{});
        draw(*frame.incoming,frame.motion);
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        if(!showing) if(auto *source=obs_weak_source_get_source(weak)) {
            obs_source_inc_showing(source); obs_source_release(source); showing=true;
        }
        channel->frame_enabled=true;refreshFrame();
    }
    void hideEvent(QHideEvent *event) override {release(); QWidget::hideEvent(event);}
public:
    Preview(obs_source_t *source,ManagerBackend *api,std::shared_ptr<Channel> updates,QWidget *parent):QWidget(parent),weak(obs_source_get_weak_source(source)),backend(api),channel(std::move(updates)) {
        setObjectName("nativePreview"); setMinimumSize(360,210);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }
    void refreshFrame() {
        if(auto *source=obs_weak_source_get_source(weak)) {
            auto latest=backend->preview_frame();obs_source_release(source);
            const bool changed=latest.incoming!=frame.incoming||latest.outgoing||frame.outgoing;
            frame=std::move(latest);
            if(changed&&isVisible())update();
        }
    }
    void setLive(bool value){if(live!=value){live=value;update();}}
    ~Preview() override {release(); obs_weak_source_release(weak);}
};
class ManagerWindow final:public QDialog {
    obs_source_t *identity=nullptr; // Map key only; not dereferenced after removal.
    obs_weak_source_t *weak=nullptr;
    ManagerBackend *backend=nullptr;
    std::shared_ptr<Channel> channel=std::make_shared<Channel>();
    std::shared_ptr<std::atomic<bool>> cancelled=std::make_shared<std::atomic<bool>>(false);
    PlaylistList *list=nullptr;
    Preview *preview=nullptr;
    QLabel *count=nullptr,*current=nullptr,*next=nullptr,*status=nullptr,*detail=nullptr,*import_note=nullptr;
    QPushButton *play=nullptr,*pause=nullptr,*previous=nullptr,*advance=nullptr;
    QComboBox *order=nullptr;
    QPushButton *quotaButton=nullptr;
    QLabel *group_summary=nullptr;
    QCheckBox *recursiveImport=nullptr;
    int group_by=0;
    QString group_separator="_";
    QHash<QString,int64_t> durations;
    QPushButton *button(const QString &caption,const char *name,QHBoxLayout *layout) {
        auto *value=new QPushButton(caption,this); value->setObjectName(name); layout->addWidget(value); return value;
    }
    QStringList paths() const {
        QStringList result; for(int i=0;i<list->count();++i) result.push_back(list->item(i)->data(Qt::UserRole).toString()); return result;
    }
    template<typename Action> void with_source(Action action) {
        if(auto *source=obs_weak_source_get_source(weak)) {action(source); obs_source_release(source);} else close();
    }
    void save(std::optional<bool> paused={}) {
        const auto values=paths();
        with_source([&](obs_source_t *source) {
            auto *settings=obs_source_get_settings(source); auto *array=obs_data_array_create();
            for(const auto &path:values) {
                auto *item=obs_data_create(); auto bytes=path.toUtf8(); obs_data_set_string(item,"value",bytes.constData());
                auto *row=list->item(int(obs_data_array_count(array)));auto manual=row->data(Qt::UserRole+1).toString().toUtf8();
                obs_data_set_string(item,"manual_group",manual.constData());
                obs_data_array_push_back(array,item); obs_data_release(item);
            }
            obs_data_set_array(settings,"files",array); obs_data_array_release(array);
            obs_data_set_bool(settings,"random",order->currentIndex()==1);
            obs_data_set_int(settings,"order_mode",order->currentIndex());
            obs_data_set_int(settings,"group_by",group_by);auto separator=group_separator.toUtf8();obs_data_set_string(settings,"group_separator",separator.constData());
            if(paused) obs_data_set_bool(settings,"paused",*paused);
            obs_source_update(source,settings); obs_data_release(settings);
            obs_source_update_properties(source);
        });
    }
    void seedManualGroups() {
        if(group_by==0||group_by==3)return;
        for(int i=0;i<list->count();++i){auto *item=list->item(i);const auto path=item->data(Qt::UserRole).toString();if(!livePath(path)&&item->data(Qt::UserRole+1).toString().isEmpty())item->setData(Qt::UserRole+1,groupLabel(path,group_by,group_separator));}
    }
    void editQuotas() {
        QPointer<ManagerWindow> alive(this);
        GroupState state;
        with_source([&](obs_source_t *){state=backend->snapshot().groups;});
        if(state.labels.empty())return;
        QDialog dialog;
        dialog.setWindowTitle(ui(Text::EntriesPerCategory));
        dialog.setStyleSheet(styleSheet());
        connect(this,&QObject::destroyed,&dialog,&QDialog::reject);
        auto *layout=new QVBoxLayout(&dialog);
        auto *hint=new QLabel(ui(Text::ChooseHowManyEntriesFromEach),&dialog);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *scroll=new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        auto *content=new QWidget;
        auto *form=new QFormLayout(content);
        scroll->setWidget(content);
        layout->addWidget(scroll,1);

        QMap<QString,QString> categories;
        for(const auto &label:state.labels){
            const auto name=text(label);
            categories.insert(name.toCaseFolded(),name.isEmpty()?ui(Text::Ungrouped):name);
        }
        auto keys=categories.keys();
        QCollator alphabet{QLocale(QLocale::Spanish)};
        alphabet.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(keys.begin(),keys.end(),[&](const auto &a,const auto &b){return alphabet.compare(categories[a],categories[b])<0;});
        std::map<std::string,QSpinBox *> selectors;
        for(const auto &key:keys){
            auto *count=new QSpinBox(content);
            count->setRange(1,99);
            const auto id=key.toUtf8().toStdString();
            const auto existing=state.quotas.find(id);
            count->setValue(existing==state.quotas.end()?1:existing->second);
            form->addRow(categories[key],count);
            selectors.emplace(id,count);
        }
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Save)->setText(ui(Text::Save));
        buttons->button(QDialogButtonBox::Cancel)->setText(ui(Text::Cancel));
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        dialog.resize(430,340);
        if(dialog.exec()!=QDialog::Accepted||!alive)return;

        with_source([&](obs_source_t *source){
            auto *settings=obs_source_get_settings(source);
            auto *array=obs_data_array_create();
            for(const auto &[key,count]:selectors){
                if(count->value()==1)continue;
                auto *item=obs_data_create();
                obs_data_set_string(item,"key",key.c_str());
                obs_data_set_int(item,"count",count->value());
                obs_data_array_push_back(array,item);
                obs_data_release(item);
            }
            obs_data_set_array(settings,"group_quotas",array);
            obs_data_array_release(array);
            obs_source_update(source,settings);
            obs_data_release(settings);
        });
    }
    void addLive() {
        QPointer<ManagerWindow> alive(this);
        QDialog dialog;
        dialog.setStyleSheet(styleSheet());dialog.setWindowTitle(ui(Text::AddObsSource));
        connect(this,&QObject::destroyed,&dialog,&QDialog::reject);
        auto *layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(ui(Text::EntryType),&dialog));
        auto *type=new QComboBox(&dialog);
        type->setObjectName("liveTypeCombo");
        type->addItems({ui(Text::Source),ui(Text::Group)});
        layout->addWidget(type);
        auto *selectionLabel=new QLabel(ui(Text::ObsSourceOrScene),&dialog);
        layout->addWidget(selectionLabel);
        auto *sources=new QComboBox(&dialog);sources->setObjectName("liveSourceCombo");layout->addWidget(sources);
        auto *groups=new QComboBox(&dialog);groups->setObjectName("liveGroupCombo");layout->addWidget(groups);
        auto collect=[](void *context,obs_source_t *source){
            auto *combo=static_cast<QComboBox *>(context);
            if(!obs_source_is_group(source)&&usable_live_source(source)){
                const auto id=QString::fromUtf8(obs_source_get_uuid(source));
                if(combo->findData(id)<0)combo->addItem(QString::fromUtf8(obs_source_get_name(source)),id);
            }
            return true;
        };
        ScenePicker picker{sources,groups,{},identity};
        obs_enum_scenes([](void *context,obs_source_t *sceneSource){
            auto &picker=*static_cast<ScenePicker *>(context);
            auto *scene=obs_scene_from_source(sceneSource);
            if(!scene)return true;
            ScenePicker child{picker.combo,picker.groups,QString::fromUtf8(obs_source_get_name(sceneSource)),picker.playlist};
            // Enumerate the items even when the scene itself would create a loop.
            obs_scene_enum_items(scene,ScenePicker::addItem,&child);
            return true;
        },&picker);
        obs_enum_sources(collect,sources);obs_enum_scenes(collect,sources);
        for(auto *combo:{sources,groups}){
            std::vector<std::pair<QString,QVariant>> entries;
            for(int i=0;i<combo->count();++i)entries.emplace_back(combo->itemText(i),combo->itemData(i));
            QCollator alphabet{QLocale(QLocale::Spanish)};
            alphabet.setCaseSensitivity(Qt::CaseInsensitive);
            alphabet.setNumericMode(true);
            std::stable_sort(entries.begin(),entries.end(),[&](const auto &a,const auto &b){return alphabet.compare(a.first,b.first)<0;});
            combo->clear();
            for(const auto &[label,id]:entries)combo->addItem(label,id);
            combo->view()->setTextElideMode(Qt::ElideMiddle);
        }
        sources->setPlaceholderText(ui(Text::NoSourcesAvailable));
        groups->setPlaceholderText(ui(Text::NoGroupsAvailable));
        layout->addWidget(new QLabel(ui(Text::DurationInSeconds),&dialog));
        auto *seconds=new QSpinBox(&dialog);seconds->setRange(1,3600);seconds->setValue(8);layout->addWidget(seconds);
        layout->addWidget(new QLabel(ui(Text::GroupForBalancedRotation),&dialog));
        auto *group=new QLineEdit(ui(Text::Camera),&dialog);layout->addWidget(group);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Ok)->setText(ui(Text::Accept));
        buttons->button(QDialogButtonBox::Cancel)->setText(ui(Text::Cancel));
        auto updatePicker=[&]{
            const bool showGroups=type->currentIndex()==1;
            sources->setVisible(!showGroups);
            groups->setVisible(showGroups);
            selectionLabel->setText(showGroups?ui(Text::ObsGroup):ui(Text::ObsSourceOrScene));
            buttons->button(QDialogButtonBox::Ok)->setEnabled((showGroups?groups:sources)->count()>0);
        };
        connect(type,&QComboBox::currentIndexChanged,&dialog,updatePicker);
        updatePicker();
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted||!alive)return;
        const auto *selected=type->currentIndex()==1?groups:sources;
        if(selected->currentIndex()<0)return;
        const auto path=QStringLiteral("obs-source:%1:%2").arg(selected->currentData().toString()).arg(seconds->value());
        for(const auto &existing:paths())if(livePath(existing)&&live_uuid(existing.toUtf8().toStdString())==live_uuid(path.toUtf8().toStdString())){
            import_note->setText(ui(Text::ThatSourceIsAlreadyInThe));return;
        }
        auto *item=new QListWidgetItem(selected->currentText(),list);
        item->setData(Qt::UserRole,path);item->setData(Qt::UserRole+1,group->text().trimmed());item->setSizeHint(QSize(200,52));
        // A named live group should work immediately even on an ungrouped playlist.
        if(group_by==0&&!group->text().trimmed().isEmpty())group_by=3;
        save();
    }
    void editGroups() {
        QPointer<ManagerWindow> alive(this);
        QDialog dialog;dialog.setStyleSheet(styleSheet());dialog.setObjectName("groupingDialog");dialog.setWindowTitle(ui(Text::OrganizeGroups));dialog.resize(620,480);
        connect(this,&QObject::destroyed,&dialog,&QDialog::reject);
        auto *layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(ui(Text::GroupBy),&dialog));
        auto *mode=new QComboBox(&dialog);mode->setObjectName("groupingMode");mode->addItems({ui(Text::NoGroups),ui(Text::NamePrefix),ui(Text::ParentSubfolder),ui(Text::Manual)});mode->setCurrentIndex(group_by);layout->addWidget(mode);
        auto *separator=new QComboBox(&dialog);separator->setObjectName("groupingSeparator");
        separator->addItem(ui(Text::UnderscoreEGAerial),"_");separator->addItem(ui(Text::HyphenEGAerial),"-");separator->addItem(ui(Text::SpaceEGAerial)," ");
        separator->setCurrentIndex(std::max(0,separator->findData(group_separator)));layout->addWidget(separator);
        auto *hint=new QLabel(ui(Text::PreviewChangesApplyWhenYouClick),&dialog);hint->setWordWrap(true);layout->addWidget(hint);
        auto *tree=new QTreeWidget(&dialog);tree->setObjectName("groupingPreview");tree->setHeaderLabels({ui(Text::GroupFile),ui(Text::Videos)});layout->addWidget(tree,1);
        auto refresh=[&]{
            tree->clear();separator->setEnabled(mode->currentIndex()==1);
            QMap<QString,QTreeWidgetItem *> groups;
            for(int i=0;i<list->count();++i){auto *item=list->item(i);auto path=item->data(Qt::UserRole).toString();
                auto manual=item->data(Qt::UserRole+1).toString();
                if(!livePath(path)&&mode->currentIndex()==3&&group_by!=0&&group_by!=3&&manual.isEmpty())manual=groupLabel(path,group_by,group_separator);
                auto label=livePath(path)?(mode->currentIndex()?manual:QString()):groupLabel(path,mode->currentIndex(),separator->currentData().toString(),manual);
                if(label.isEmpty())label=mode->currentIndex()==0?ui(Text::AllVideos):ui(Text::Ungrouped);
                auto key=label.toCaseFolded();auto *parent=groups.value(key,nullptr);
                if(!parent){parent=new QTreeWidgetItem(tree,{label,"0"});groups.insert(key,parent);}
                parent->setText(1,QString::number(parent->childCount()+1));new QTreeWidgetItem(parent,{entryName(path)});
            }
            tree->resizeColumnToContents(0);
            hint->setText(QString(ui(Text::GroupsEachGroupPlaysItsConfigured)).arg(groups.size()).arg(mode->currentIndex()==3?ui(Text::UseAssignGroupInThePlaylist):mode->currentIndex()==1?ui(Text::UsesTheTextBeforeTheFirst):mode->currentIndex()==2?ui(Text::EachParentFolderIsAGroup):ui(Text::AllVideosShareOneShufflePool)));
        };
        connect(mode,&QComboBox::currentIndexChanged,&dialog,[&](int){refresh();});connect(separator,&QComboBox::currentIndexChanged,&dialog,[&](int){refresh();});refresh();
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
        buttons->button(QDialogButtonBox::Save)->setText(ui(Text::Save));
        buttons->button(QDialogButtonBox::Cancel)->setText(ui(Text::Cancel));
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        const auto result=dialog.exec();if(!alive)return;
        if(result==QDialog::Accepted){if(mode->currentIndex()==3)seedManualGroups();group_by=mode->currentIndex();group_separator=separator->currentData().toString();save();}
    }
    void append(QStringList values) {
        QSet<QString> existing; for(auto &path:paths()) existing.insert(QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded());
        int added=0;
        for(const auto &path:values) {
            auto normalized=QDir::cleanPath(QDir::fromNativeSeparators(path)); auto key=normalized.toCaseFolded();
            if(existing.contains(key)) continue;
            auto *item=new QListWidgetItem(QFileInfo(path).fileName(),list); item->setData(Qt::UserRole,path); item->setSizeHint(QSize(200,52));
            existing.insert(key); ++added;
        }
        import_note->setText(added?QString(ui(Text::VideosAdded)).arg(added):ui(Text::NoNewVideosAdded));
        if(added) save();
    }
    void importPaths(QStringList values) {
        if(values.isEmpty()) return;
        import_note->setText(ui(Text::LookingForVideos));
        auto result=channel; auto cancel=cancelled;const bool recursive=recursiveImport->isChecked();
        imports->start([values=std::move(values),result,cancel,recursive] {
            QStringList found;
            for(const auto &value:values) {
                if(*cancel) return;
                QFileInfo info(value);
                if(info.isDir()) {
                    QDirIterator iterator(value,QDir::Files|QDir::Readable,recursive?QDirIterator::Subdirectories:QDirIterator::NoIteratorFlags);
                    QStringList folder;
                    while(iterator.hasNext()&&!*cancel) {iterator.next(); if(supported(iterator.fileInfo())) folder.push_back(iterator.fileInfo().absoluteFilePath());}
                    QCollator collator; collator.setNumericMode(true);
                    std::sort(folder.begin(),folder.end(),[&](const auto &a,const auto &b){return collator.compare(a,b)<0;});
                    found+=folder;
                } else if(supported(info)) found.push_back(info.absoluteFilePath());
            }
            if(!*cancel) result->files(std::move(found));
        });
    }
public:
    ManagerWindow(obs_source_t *source,ManagerBackend *api):identity(source),weak(obs_source_get_weak_source(source)),backend(api) {
        setObjectName("nexusPlaylistManager"); setWindowTitle(ui(Text::PlaylistManagerObsPlaylistNexus));
        setAttribute(Qt::WA_DeleteOnClose); resize(1120,700); setMinimumSize(920,580);
        setStyleSheet(R"(
            QDialog#nexusPlaylistManager, QDialog#groupingDialog, QInputDialog {background:#10121b;color:#edf0fa;font-family:'Segoe UI';font-size:13px;}
            QCheckBox {color:#d8dced;}
            QTreeWidget, QLineEdit {background:#121623;color:#dfe3f4;border:1px solid #2e354b;}
            QLabel {color:#d8dced;background:transparent;}
            QLabel#brand {color:#bb94ff;font-size:12px;font-weight:700;letter-spacing:2px;}
            QLabel#heading {color:#f5f3ff;font-size:25px;font-weight:700;}
            QLabel#section {font-size:15px;font-weight:600;color:#efedfa;}
            QLabel#currentFile {font-size:17px;font-weight:600;color:#f4f1ff;}
            QLabel#muted {color:#858da7;font-size:12px;}
            QFrame#card {background:#191d2a;border:1px solid #2a3042;border-radius:12px;}
            QPushButton {background:#252b3c;color:#e7eafa;border:1px solid #384057;border-radius:6px;padding:9px 12px;}
            QPushButton:hover {background:#34334e;border-color:#9672d8;}
            QPushButton:disabled {color:#626b83;background:#1d2230;border-color:#282e40;}
            QPushButton#playButton {background:#8552d4;border-color:#a776ee;color:white;font-weight:600;}
            QPushButton#playButton:disabled {background:#35294b;color:#8d7ca3;border-color:#483759;}
            QListWidget {background:#121623;color:#dfe3f4;border:1px solid #2e354b;border-radius:7px;outline:none;padding:5px;}
            QListWidget::item {padding:6px;border:1px solid transparent;border-radius:5px;}
            QListWidget::item:selected {background:#332849;border-color:#8e64c5;}
            QListWidget::item:hover {background:#23283a;}
            QComboBox {background:#252b3c;color:#eff0fa;border:1px solid #384057;border-radius:6px;padding:9px;}
            QComboBox QAbstractItemView {background:#252b3c;color:#edf0fa;selection-background-color:#7952b4;}
            QScrollBar:vertical {background:#151927;width:10px;} QScrollBar::handle:vertical {background:#40475e;border-radius:4px;min-height:30px;}
        )");
        auto *root=new QVBoxLayout(this); root->setContentsMargins(24,22,24,18); root->setSpacing(12);
        auto *brand=new QLabel(ui(Text::NexusVideoPlaylist),this); brand->setObjectName("brand"); root->addWidget(brand);
        auto *heading=new QLabel(ui(Text::PlaylistManager),this); heading->setObjectName("heading"); root->addWidget(heading);
        auto *subtitle=new QLabel(ui(Text::YourVideosPlayingContinuously),this); subtitle->setObjectName("muted"); root->addWidget(subtitle);
        auto *columns=new QHBoxLayout; columns->setSpacing(16); root->addLayout(columns,1);
        auto *left=new QFrame(this); left->setObjectName("card"); columns->addWidget(left,6);
        auto *leftLayout=new QVBoxLayout(left); leftLayout->setContentsMargins(16,16,16,14); leftLayout->setSpacing(12);
        auto *listHeading=new QLabel(ui(Text::Playlist),left); listHeading->setObjectName("section"); leftLayout->addWidget(listHeading);
        auto *addRow=new QHBoxLayout;
        auto *add=button(ui(Text::AddVideos),"addFilesButton",addRow);
        auto *folder=button(ui(Text::AddFolder),"addFolderButton",addRow);
        auto *remove=button(ui(Text::Remove),"removeButton",addRow); leftLayout->addLayout(addRow);
        auto *liveButton=new QPushButton(ui(Text::AddObsSource),left);liveButton->setObjectName("addLiveButton");leftLayout->addWidget(liveButton);
        connect(liveButton,&QPushButton::clicked,this,[this]{addLive();});
        list=new PlaylistList(left); leftLayout->addWidget(list,1);
        list->reordered=[this]{save();}; list->dropped=[this](QStringList paths){importPaths(std::move(paths));};
        auto *moveRow=new QHBoxLayout; auto *up=button(ui(Text::MoveUp),"moveUpButton",moveRow); auto *down=button(ui(Text::MoveDown),"moveDownButton",moveRow);
        moveRow->addStretch(); count=new QLabel(left); count->setObjectName("playlistCount"); moveRow->addWidget(count); leftLayout->addLayout(moveRow);
        import_note=new QLabel(ui(Text::DragVideosOrAFolderInto),left); import_note->setObjectName("muted"); leftLayout->addWidget(import_note);
        auto *groupRow=new QHBoxLayout;auto *groupButton=button(ui(Text::OrganizeGroups),"groupingButton",groupRow);auto *assignButton=button(ui(Text::AssignGroup),"assignGroupButton",groupRow);leftLayout->addLayout(groupRow);
        group_summary=new QLabel(left);group_summary->setObjectName("groupSummary");group_summary->setWordWrap(true);leftLayout->addWidget(group_summary);
        quotaButton=new QPushButton(ui(Text::EntriesPerCategory2),left);
        leftLayout->addWidget(quotaButton);
        quotaButton->hide();
        connect(quotaButton,&QPushButton::clicked,this,[this]{editQuotas();});
        recursiveImport=new QCheckBox(ui(Text::IncludeSubfoldersWhenImporting),left);recursiveImport->setObjectName("recursiveImport");leftLayout->addWidget(recursiveImport);
        auto *right=new QFrame(this); right->setObjectName("card"); columns->addWidget(right,5);
        auto *rightLayout=new QVBoxLayout(right); rightLayout->setContentsMargins(16,16,16,16); rightLayout->setSpacing(12);
        auto *playHeading=new QLabel(ui(Text::Playback),right); playHeading->setObjectName("section"); rightLayout->addWidget(playHeading);
        preview=new Preview(source,backend,channel,right); rightLayout->addWidget(preview,1);
        auto *nowLabel=new QLabel(ui(Text::NowPlaying),right); nowLabel->setObjectName("muted"); rightLayout->addWidget(nowLabel);
        current=new QLabel(right); current->setObjectName("currentFile"); current->setWordWrap(true); rightLayout->addWidget(current);
        next=new QLabel(right); next->setObjectName("nextFile"); next->setWordWrap(true); rightLayout->addWidget(next);
        auto *controls=new QHBoxLayout; controls->setSpacing(6);
        previous=button(ui(Text::Previous),"previousButton",controls); play=button(ui(Text::Play),"playButton",controls);
        pause=button(ui(Text::Pause),"pauseButton",controls); advance=button(ui(Text::Next),"nextButton",controls); rightLayout->addLayout(controls);
        auto *orderRow=new QHBoxLayout; orderRow->addWidget(new QLabel(ui(Text::Order),right));
        order=new QComboBox(right); order->setObjectName("orderCombo"); order->addItems({ui(Text::Sequential),ui(Text::Shuffle),ui(Text::BalancedGroupShuffle)}); orderRow->addWidget(order,1); rightLayout->addLayout(orderRow);
        auto *loop=new QLabel(ui(Text::ContinuousLoop),right); loop->setObjectName("muted"); rightLayout->addWidget(loop);
        status=new QLabel(this); status->setObjectName("preloadStatus"); root->addWidget(status);
        detail=new QLabel(this); detail->setObjectName("statusDetail"); detail->setWordWrap(true); root->addWidget(detail);
        connect(add,&QPushButton::clicked,this,[this] {importPaths(QFileDialog::getOpenFileNames(this,ui(Text::AddVideos),{},ui(Text::VideosMpMkvMovWebm)));});
        connect(folder,&QPushButton::clicked,this,[this] {auto path=QFileDialog::getExistingDirectory(this,ui(Text::AddFolder)); if(!path.isEmpty()) importPaths({path});});
        connect(remove,&QPushButton::clicked,this,[this] {qDeleteAll(list->selectedItems()); save();});
        connect(up,&QPushButton::clicked,this,[this] {int row=list->currentRow(); if(row>0){auto *item=list->takeItem(row);list->insertItem(row-1,item);list->setCurrentItem(item);save();}});
        connect(down,&QPushButton::clicked,this,[this] {int row=list->currentRow();if(row>=0&&row+1<list->count()){auto *item=list->takeItem(row);list->insertItem(row+1,item);list->setCurrentItem(item);save();}});
        connect(play,&QPushButton::clicked,this,[this]{save(false);}); connect(pause,&QPushButton::clicked,this,[this]{save(true);});
        connect(previous,&QPushButton::clicked,this,[this]{with_source([&](obs_source_t *){backend->navigate(-1);});});
        connect(advance,&QPushButton::clicked,this,[this]{with_source([&](obs_source_t *){backend->navigate(1);});});
        connect(list,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem *item){with_source([&](obs_source_t *){backend->jump(size_t(list->row(item)));});});
        connect(order,&QComboBox::currentIndexChanged,this,[this](int){save();});
        connect(groupButton,&QPushButton::clicked,this,[this]{editGroups();});
        connect(assignButton,&QPushButton::clicked,this,[this]{
            if(list->selectedItems().isEmpty()){import_note->setText(ui(Text::SelectTheVideosYouWantTo));return;}
            QPointer<ManagerWindow> alive(this);QInputDialog dialog;dialog.setStyleSheet(styleSheet());dialog.setWindowTitle(ui(Text::AssignGroup));dialog.setLabelText(ui(Text::GroupNameBlankUngrouped));dialog.setInputMode(QInputDialog::TextInput);
            dialog.setOkButtonText(ui(Text::Accept));
            dialog.setCancelButtonText(ui(Text::Cancel));
            connect(this,&QObject::destroyed,&dialog,&QInputDialog::reject);
            const auto result=dialog.exec();if(!alive)return;
            if(result==QDialog::Accepted){seedManualGroups();for(auto *item:list->selectedItems())item->setData(Qt::UserRole+1,dialog.textValue().trimmed());group_by=3;save();}
        });
        channel->receiver=this; channel->refresh=[this]{refresh();};channel->frame_refresh=[this]{preview->refreshFrame();}; channel->close=[this]{close();}; channel->imported=[this](QStringList paths){append(std::move(paths));};
        backend->subscribe(channel); refresh();
    }
    void refresh() {
        with_source([&](obs_source_t *) {
            const auto state=backend->snapshot(); QStringList values;
            group_by=state.groups.by;group_separator=text(state.groups.separator);
            quotaButton->setVisible(state.order==2);
            quotaButton->setEnabled(!state.groups.labels.empty());
            for(const auto &path:state.files) values.push_back(text(path));
            if(paths()!=values) {
                list->clear(); for(const auto &path:values) {auto *item=new QListWidgetItem(QString(),list);item->setData(Qt::UserRole,path);item->setSizeHint(QSize(200,52));}
            }
            const auto actual=text(state.current),following=text(state.next);
            if(state.current_duration>0) durations[actual]=state.current_duration;
            if(state.next_duration>0) durations[following]=state.next_duration;
            for(int i=0;i<list->count();++i) {
                auto *item=list->item(i); auto path=item->data(Qt::UserRole).toString();
                item->setData(Qt::UserRole+1,i<int(state.groups.manual.size())?text(state.groups.manual[size_t(i)]):QString());
                const bool live=livePath(path);
                auto label=entryName(path)+"\n"+(live?QString(ui(Text::SLive)).arg(live_seconds(path.toUtf8().toStdString())):duration(durations.value(path))+"  ·  "+QFileInfo(path).suffix().toUpper());
                if(group_by!=0)label+="  ·  ["+(i<int(state.groups.labels.size())&&!state.groups.labels[size_t(i)].empty()?text(state.groups.labels[size_t(i)]):QString(ui(Text::Ungrouped)))+"]";
                if(path==actual) label+=(state.paused||state.hidden)?ui(Text::CurrentPaused):ui(Text::Playing);
                else if(path==following) label+=state.ready?ui(Text::NextReady):ui(Text::Preparing);
                item->setText(label); item->setToolTip(path);
                item->setForeground(path==actual?QColor("#c8a7ff"):path==following&&state.ready?QColor("#82dcb0"):QColor("#dfe3f4"));
            }
            {QSignalBlocker block(order);order->setCurrentIndex(state.order);}
            QMap<QString,int> groupCounts;for(auto &label:state.groups.labels)++groupCounts[text(label).toCaseFolded()];
            group_summary->setText(group_by==0?ui(Text::NoGroupsAllVideosShareOne):QString(ui(Text::GroupsVideosUnassignedEntriesAreGrouped)).arg(groupCounts.size()).arg(values.size()));
            current->setText(actual.isEmpty()?ui(Text::AddVideosToGetStarted):entryName(actual)); current->setToolTip(actual);
            next->setText(following.isEmpty()?ui(Text::Next2):ui(Text::Next3)+entryName(following)); next->setToolTip(following);
            preview->setLive(livePath(actual));
            count->setText(QString(ui(Text::Videos2)).arg(values.size()));
            const bool available=!values.isEmpty(),busy=state.navigating;
            previous->setEnabled(available&&!busy); advance->setEnabled(available&&!busy);
            play->setEnabled(available&&!busy&&(state.paused||state.exhausted)); pause->setEnabled(available&&!state.paused&&!busy);
            auto health=!available?QString(ui(Text::NoVideos)):state.exhausted?QString(ui(Text::ErrorNoPlayableVideos)):state.ready?QString(ui(Text::ReadyNextVideoPrepared)):QString(ui(Text::PreparingNextVideo));
            if(state.paused) health=ui(Text::Paused)+health; else if(state.hidden) health=ui(Text::SourceHidden)+health;
            if(busy) health=ui(Text::PreparingSkip)+entryName(following);
            status->setText(health); status->setStyleSheet(QString("color:%1;font-weight:600;").arg(state.exhausted?"#ff8e9e":state.ready?"#82dcb0":"#d6ba82"));
            detail->setText(!state.error.empty()?text(state.error):state.failures?QString(ui(Text::ContinuityIssues)).arg(state.failures):!actual.isEmpty()&&!values.contains(actual)?ui(Text::TheCurrentVideoWillFinishBefore):ui(Text::PlaybackContinuesWhileYouReorderThe));
            preview->refreshFrame();
        });
    }
    ~ManagerWindow() override {
        *cancelled=true; channel->detach();
        if(auto *source=obs_weak_source_get_source(weak)) {backend->unsubscribe(channel.get());obs_source_release(source);}
        delete preview; preview=nullptr;
        obs_weak_source_release(weak);
        auto found=windows.find(identity); if(found!=windows.end()&&found->second==this) windows.erase(found);
    }
protected:
    void showEvent(QShowEvent *event) override {QDialog::showEvent(event);refresh();}
};
}
void open_manager(obs_source_t *source,ManagerBackend *backend) {
    if(!qApp||QThread::currentThread()!=qApp->thread()) {blog(LOG_WARNING,"[Playlist NEXUS] Manager requires the Qt GUI thread");return;}
    if(auto found=windows.find(source);found!=windows.end()&&found->second) {found->second->show();found->second->raise();found->second->activateWindow();return;}
    if(!imports) {
        imports=new QThreadPool;imports->setMaxThreadCount(1);imports->setExpiryTimeout(30000);
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,qApp,[]{shutdown_managers();});
    }
    auto *window=new ManagerWindow(source,backend); windows[source]=window; window->show();
}
void shutdown_managers() {
    auto copy=windows; for(auto &[key,window]:copy) if(window) delete window.data(); windows.clear();
    if(imports) {imports->clear();imports->waitForDone();delete imports;imports=nullptr;}
}
}
