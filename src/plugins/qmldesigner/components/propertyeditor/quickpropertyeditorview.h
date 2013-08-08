/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#ifndef QUICKPROERTYEDITORVIEW_H
#define QUICKPROERTYEDITORVIEW_H

#include <QWidget>
#include <QUrl>
#include <QDeclarativeEngine>
#include <QDeclarativeContext>

QT_BEGIN_NAMESPACE
class QDeclarativeContext;
class QDeclarativeError;
class QDeclarativeComponent;
QT_END_NAMESPACE

namespace QmlDesigner {

class QuickPropertyEditorView : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(QUrl source READ source WRITE setSource DESIGNABLE true)
public:
    explicit QuickPropertyEditorView(QWidget *parent = 0);

    QUrl source() const;
    void setSource(const QUrl&);

    QDeclarativeEngine* engine();
    QDeclarativeContext* rootContext();

    QWidget *rootWidget() const;

    enum Status { Null, Ready, Loading, Error };
    Status status() const;

    static void registerQmlTypes();

signals:
    void statusChanged(QuickPropertyEditorView::Status);

protected:
    void setRootWidget(QWidget *);
    void execute();

private Q_SLOTS:
    void continueExecute();

private:
     QScopedPointer<QWidget> m_root;
     QUrl m_source;
     QDeclarativeEngine m_engine;
     QWeakPointer<QDeclarativeComponent> m_component;

};

} //QmlDesigner

#endif // QUICKPROERTYEDITORVIEW_H
