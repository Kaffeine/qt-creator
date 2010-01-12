/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#ifndef QMLMETATYPEBACKEND_H
#define QMLMETATYPEBACKEND_H

#include <qml/qml_global.h>
#include <qml/qmlpackageinfo.h>
#include <qml/qmlsymbol.h>

namespace Qml {

class QmlTypeSystem;

class QML_EXPORT QmlMetaTypeBackend
{
public:
    QmlMetaTypeBackend(QmlTypeSystem *typeSystem);
    virtual ~QmlMetaTypeBackend() = 0;

    virtual QList<QmlSymbol *> availableTypes(const QString &package, int majorVersion, int minorVersion) = 0;
    virtual QmlSymbol *resolve(const QString &typeName, const QList<PackageInfo> &packages) = 0;

protected:
    QmlTypeSystem *typeSystem() const
    { return m_typeSystem; }

private:
    QmlTypeSystem *m_typeSystem;
};

} // namespace Qml

#endif // QMLMETATYPEBACKEND_H
