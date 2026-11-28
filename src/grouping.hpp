#pragma once
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace nexus {
inline QString groupLabel(const QString &path,int by,const QString &separator,const QString &manual={}) {
    if(by==0)return {};
    if(by==3)return manual.trimmed();
    QFileInfo info(QDir::fromNativeSeparators(path));
    if(by==2)return info.dir().dirName();
    auto stem=info.completeBaseName();int boundary=separator.isEmpty()?-1:stem.indexOf(separator);
    return (boundary<0?stem:stem.left(boundary)).trimmed();
}
}
