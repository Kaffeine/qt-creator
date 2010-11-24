/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
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

#include "breakwindow.h"
#include "breakhandler.h"

#include "debuggeractions.h"
#include "debuggercore.h"
#include "ui_breakpoint.h"
#include "ui_breakcondition.h"

#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <QtCore/QDebug>

#include <QtGui/QAction>
#include <QtGui/QHeaderView>
#include <QtGui/QIntValidator>
#include <QtGui/QItemSelectionModel>
#include <QtGui/QKeyEvent>
#include <QtGui/QMenu>
#include <QtGui/QResizeEvent>
#include <QtGui/QToolButton>
#include <QtGui/QTreeView>


namespace Debugger {
namespace Internal {


///////////////////////////////////////////////////////////////////////
//
// BreakpointDialog: Show a dialog for editing breakpoints. Shows controls
// for the file-and-line, function and address parameters depending on the
// breakpoint type. The controls not applicable to the current type
// (say function name for file-and-line) are disabled and cleared out.
// However,the values are saved and restored once the respective mode
// is again choosen, which is done using m_savedParameters and
// setters/getters taking the parts mask enumeration parameter.
//
///////////////////////////////////////////////////////////////////////

class BreakpointDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BreakpointDialog(QWidget *parent);
    bool showDialog(BreakpointParameters *data);

    void setParameters(const BreakpointParameters &p);
    BreakpointParameters parameters() const;

public slots:
    void typeChanged(int index);

private:
    enum DialogPart { FileAndLinePart = 0x1,
                      FunctionPart = 0x2,
                      AddressPart = 0x4,
                      AllParts = FileAndLinePart|FunctionPart|AddressPart };

    void setPartsEnabled(unsigned partsMask, bool e);
    void clearParts(unsigned partsMask);
    void getParts(unsigned partsMask, BreakpointParameters *p) const;
    void setParts(unsigned partsMask, const BreakpointParameters &p);

    void setType(BreakpointType type);
    BreakpointType type() const;

    Ui::BreakpointDialog m_ui;
    BreakpointParameters m_savedParameters;
    BreakpointType m_previousType;
};

BreakpointDialog::BreakpointDialog(QWidget *parent) :
    QDialog(parent), m_previousType(UnknownType)
{
    // match BreakpointType (omitting unknown type)
    m_ui.setupUi(this);
    QStringList types;
    types << tr("File and Line Number") << tr("Function Name") << tr("Address")
          << tr("throw") << tr("catch") << tr("Function \"main()\"")
          << tr("Address (Watchpoint)");
    QTC_ASSERT(types.size() == Watchpoint, return; )
    m_ui.comboBoxType->addItems(types);
    m_ui.pathChooserFileName->setExpectedKind(Utils::PathChooser::File);
    connect(m_ui.comboBoxType, SIGNAL(activated(int)), SLOT(typeChanged(int)));
    m_ui.lineEditIgnoreCount->setValidator(
        new QIntValidator(0, 2147483647, m_ui.lineEditIgnoreCount));
}

void BreakpointDialog::setType(BreakpointType type)
{
    const int comboIndex = type - 1; // Skip UnknownType
    if (comboIndex != m_ui.comboBoxType->currentIndex()) {
        m_ui.comboBoxType->setCurrentIndex(comboIndex);
        typeChanged(comboIndex);
    }
}

BreakpointType BreakpointDialog::type() const
{
    const int type = m_ui.comboBoxType->currentIndex() + 1; // Skip unknown type
    return static_cast<BreakpointType>(type);
}

void BreakpointDialog::setParameters(const BreakpointParameters &p)
{
    m_savedParameters = p;
    setType(p.type);
    setParts(AllParts, p);
    m_ui.lineEditCondition->setText(QString::fromUtf8(p.condition));
    m_ui.lineEditIgnoreCount->setText(QString::number(p.ignoreCount));
    m_ui.lineEditThreadSpec->setText(p.threadSpec);
}

BreakpointParameters BreakpointDialog::parameters() const
{
    BreakpointParameters rc(type());
    getParts(AllParts, &rc);
    rc.condition = m_ui.lineEditCondition->text().toUtf8();
    rc.ignoreCount = m_ui.lineEditIgnoreCount->text().toInt();
    rc.threadSpec = m_ui.lineEditThreadSpec->text().toUtf8();
    return rc;
}

void BreakpointDialog::setPartsEnabled(unsigned partsMask, bool e)
{
    if (partsMask & FileAndLinePart) {
        m_ui.labelFileName->setEnabled(e);
        m_ui.pathChooserFileName->setEnabled(e);
        m_ui.labelLineNumber->setEnabled(e);
        m_ui.lineEditLineNumber->setEnabled(e);
        m_ui.labelUseFullPath->setEnabled(e);
        m_ui.checkBoxUseFullPath->setEnabled(e);
    }

    if (partsMask & FunctionPart) {
        m_ui.labelFunction->setEnabled(e);
        m_ui.lineEditFunction->setEnabled(e);
    }

    if (partsMask & AddressPart) {
        m_ui.labelAddress->setEnabled(e);
        m_ui.lineEditAddress->setEnabled(e);
    }
}

void BreakpointDialog::clearParts(unsigned partsMask)
{
    if (partsMask & FileAndLinePart) {
        m_ui.pathChooserFileName->setPath(QString());
        m_ui.lineEditLineNumber->clear();
        m_ui.checkBoxUseFullPath->setChecked(false);
    }

    if (partsMask & FunctionPart)
        m_ui.lineEditFunction->clear();

    if (partsMask & AddressPart)
        m_ui.lineEditAddress->clear();
}

void BreakpointDialog::getParts(unsigned partsMask, BreakpointParameters *p) const
{
    if (partsMask & FileAndLinePart) {
        p->lineNumber = m_ui.lineEditLineNumber->text().toInt();
        p->useFullPath = m_ui.checkBoxUseFullPath->isChecked();
        p->fileName = m_ui.pathChooserFileName->path();
    }
    if (partsMask & FunctionPart)
        p->functionName = m_ui.lineEditFunction->text();

    if (partsMask & AddressPart)
        p->address = m_ui.lineEditAddress->text().toULongLong(0, 0);
}

void BreakpointDialog::setParts(unsigned mask, const BreakpointParameters &p)
{
    if (mask & FileAndLinePart) {
        m_ui.pathChooserFileName->setPath(p.fileName);
        m_ui.lineEditLineNumber->setText(QString::number(p.lineNumber));
        m_ui.checkBoxUseFullPath->setChecked(p.useFullPath);
    }

    if (mask & FunctionPart)
        m_ui.lineEditFunction->setText(p.functionName);

    if (mask & AddressPart) {
        if (p.address) {
            m_ui.lineEditAddress->setText(QString::fromAscii("0x%1").arg(p.address, 0, 16));
        } else {
            m_ui.lineEditAddress->clear();
        }
    }
}

void BreakpointDialog::typeChanged(int)
{
    BreakpointType previousType = m_previousType;
    const BreakpointType newType = type();
    m_previousType = newType;
    switch(previousType) { // Save current state
    case UnknownType:
        break;
    case BreakpointByFileAndLine:
        getParts(FileAndLinePart, &m_savedParameters);
        break;
    case BreakpointByFunction:
        getParts(FunctionPart, &m_savedParameters);
        break;
    case BreakpointAtThrow:
    case BreakpointAtCatch:
    case BreakpointAtMain:
        break;
    case BreakpointByAddress:
    case Watchpoint:
        getParts(AddressPart, &m_savedParameters);
        break;
    }

    switch (newType) { // Enable and set up new state from saved values
    case UnknownType:
        break;
    case BreakpointByFileAndLine:
        setParts(FileAndLinePart, m_savedParameters);
        setPartsEnabled(FileAndLinePart, true);
        clearParts(FunctionPart|AddressPart);
        setPartsEnabled(FunctionPart|AddressPart, false);
        break;
    case BreakpointByFunction:
        setParts(FunctionPart, m_savedParameters);
        setPartsEnabled(FunctionPart, true);
        clearParts(FileAndLinePart|AddressPart);
        setPartsEnabled(FileAndLinePart|AddressPart, false);
        break;
    case BreakpointAtThrow:
    case BreakpointAtCatch:
        clearParts(AllParts);
        setPartsEnabled(AllParts, false);
        break;
    case BreakpointAtMain:
        m_ui.lineEditFunction->setText(QLatin1String("main")); // Just for display
        clearParts(FileAndLinePart|AddressPart);
        setPartsEnabled(AllParts, false);
        break;
    case BreakpointByAddress:
    case Watchpoint:
        setParts(AddressPart, m_savedParameters);
        setPartsEnabled(AddressPart, true);
        clearParts(FileAndLinePart|FunctionPart);
        setPartsEnabled(FileAndLinePart|FunctionPart, false);
        break;
    }
}

bool BreakpointDialog::showDialog(BreakpointParameters *data)
{
    setParameters(*data);
    if (exec() != QDialog::Accepted)
        return false;

    // Check if changed.
    const BreakpointParameters newParameters = parameters();
    if (data->equals(newParameters))
        return false;

    *data = newParameters;
    return true;
}

///////////////////////////////////////////////////////////////////////
//
// BreakWindow
//
///////////////////////////////////////////////////////////////////////

BreakWindow::BreakWindow(QWidget *parent)
  : QTreeView(parent)
{
    m_alwaysResizeColumnsToContents = false;

    QAction *act = debuggerCore()->action(UseAlternatingRowColors);
    setFrameStyle(QFrame::NoFrame);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setWindowTitle(tr("Breakpoints"));
    setWindowIcon(QIcon(QLatin1String(":/debugger/images/debugger_breakpoints.png")));
    setAlternatingRowColors(act->isChecked());
    setRootIsDecorated(false);
    setIconSize(QSize(10, 10));
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    connect(this, SIGNAL(activated(QModelIndex)),
        SLOT(rowActivated(QModelIndex)));
    connect(act, SIGNAL(toggled(bool)),
        SLOT(setAlternatingRowColorsHelper(bool)));
    connect(debuggerCore()->action(UseAddressInBreakpointsView), SIGNAL(toggled(bool)),
        SLOT(showAddressColumn(bool)));
}

BreakWindow::~BreakWindow()
{
}

void BreakWindow::showAddressColumn(bool on)
{
    setColumnHidden(7, !on);
}

void BreakWindow::keyPressEvent(QKeyEvent *ev)
{
    if (ev->key() == Qt::Key_Delete) {
        QItemSelectionModel *sm = selectionModel();
        QTC_ASSERT(sm, return);
        QModelIndexList si = sm->selectedIndexes();
        if (si.isEmpty())
            si.append(currentIndex());
        const BreakpointIds ids = breakHandler()->findBreakpointsByIndex(si);
        int row = qMin(model()->rowCount() - ids.size() - 1, currentIndex().row());
        deleteBreakpoints(ids);
        setCurrentIndex(si.at(0).sibling(row, 0));
    }
    QTreeView::keyPressEvent(ev);
}

void BreakWindow::resizeEvent(QResizeEvent *ev)
{
    QTreeView::resizeEvent(ev);
}

void BreakWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
    QModelIndex indexUnderMouse = indexAt(ev->pos());
    if (indexUnderMouse.isValid() && indexUnderMouse.column() >= 4) {
        BreakpointId id = breakHandler()->findBreakpointByIndex(indexUnderMouse);
        editBreakpoints(BreakpointIds() << id);
    }
    QTreeView::mouseDoubleClickEvent(ev);
}

void BreakWindow::contextMenuEvent(QContextMenuEvent *ev)
{
    QMenu menu;
    QItemSelectionModel *sm = selectionModel();
    QTC_ASSERT(sm, return);
    QModelIndexList selectedIndices = sm->selectedIndexes();
    QModelIndex indexUnderMouse = indexAt(ev->pos());
    if (selectedIndices.isEmpty() && indexUnderMouse.isValid())
        selectedIndices.append(indexUnderMouse);

    BreakHandler *handler = breakHandler();
    BreakpointIds selectedIds = handler->findBreakpointsByIndex(selectedIndices);

    const int rowCount = model()->rowCount();
    const unsigned engineCapabilities = BreakOnThrowAndCatchCapability;
    // FIXME BP:    model()->data(QModelIndex(), EngineCapabilitiesRole).toUInt();

    QAction *deleteAction = new QAction(tr("Delete Breakpoint"), &menu);
    deleteAction->setEnabled(!selectedIds.isEmpty());

    QAction *deleteAllAction = new QAction(tr("Delete All Breakpoints"), &menu);
    deleteAllAction->setEnabled(model()->rowCount() > 0);

    // Delete by file: Find indices of breakpoints of the same file.
    QAction *deleteByFileAction = 0;
    BreakpointIds breakpointsInFile;
    if (indexUnderMouse.isValid()) {
        const QModelIndex index = indexUnderMouse.sibling(indexUnderMouse.row(), 2);
        const QString file = index.data().toString();
        if (!file.isEmpty()) {
            for (int i = 0; i < rowCount; i++)
                if (index.data().toString() == file)
                    breakpointsInFile.append(handler->findBreakpointByIndex(index));
            if (breakpointsInFile.size() > 1) {
                deleteByFileAction =
                    new QAction(tr("Delete Breakpoints of \"%1\"").arg(file), &menu);
                deleteByFileAction->setEnabled(true);
            }
        }
    }
    if (!deleteByFileAction) {
        deleteByFileAction = new QAction(tr("Delete Breakpoints of File"), &menu);
        deleteByFileAction->setEnabled(false);
    }

    QAction *adjustColumnAction =
        new QAction(tr("Adjust Column Widths to Contents"), &menu);

    QAction *alwaysAdjustAction =
        new QAction(tr("Always Adjust Column Widths to Contents"), &menu);

    alwaysAdjustAction->setCheckable(true);
    alwaysAdjustAction->setChecked(m_alwaysResizeColumnsToContents);

    QAction *editBreakpointAction =
        new QAction(tr("Edit Breakpoint..."), &menu);
    editBreakpointAction->setEnabled(!selectedIds.isEmpty());

    int threadId = 0;
    // FIXME BP: m_engine->threadsHandler()->currentThreadId();
    QString associateTitle = threadId == -1
        ?  tr("Associate Breakpoint With All Threads")
        :  tr("Associate Breakpoint With Thread %1").arg(threadId);
    QAction *associateBreakpointAction = new QAction(associateTitle, &menu);
    associateBreakpointAction->setEnabled(!selectedIds.isEmpty());

    QAction *synchronizeAction =
        new QAction(tr("Synchronize Breakpoints"), &menu);
    synchronizeAction->setEnabled(debuggerCore()->hasSnapshots());

    bool enabled = selectedIds.isEmpty() || handler->isEnabled(selectedIds.at(0));

    const QString str5 = selectedIds.size() > 1
        ? enabled
            ? tr("Disable Selected Breakpoints")
            : tr("Enable Selected Breakpoints")
        : enabled
            ? tr("Disable Breakpoint")
            : tr("Enable Breakpoint");
    QAction *toggleEnabledAction = new QAction(str5, &menu);
    toggleEnabledAction->setEnabled(!selectedIds.isEmpty());

    QAction *addBreakpointAction =
        new QAction(tr("Add Breakpoint..."), this);
    QAction *breakAtThrowAction =
        new QAction(tr("Set Breakpoint at \"throw\""), this);
    QAction *breakAtCatchAction =
        new QAction(tr("Set Breakpoint at \"catch\""), this);

    menu.addAction(addBreakpointAction);
    menu.addAction(deleteAction);
    menu.addAction(editBreakpointAction);
    menu.addAction(associateBreakpointAction);
    menu.addAction(toggleEnabledAction);
    menu.addSeparator();
    menu.addAction(deleteAllAction);
    //menu.addAction(deleteByFileAction);
    menu.addSeparator();
    menu.addAction(synchronizeAction);
    if (engineCapabilities & BreakOnThrowAndCatchCapability) {
        menu.addSeparator();
        menu.addAction(breakAtThrowAction);
        menu.addAction(breakAtCatchAction);
    }
    menu.addSeparator();
    menu.addAction(debuggerCore()->action(UseToolTipsInBreakpointsView));
    menu.addAction(debuggerCore()->action(UseAddressInBreakpointsView));
    menu.addAction(adjustColumnAction);
    menu.addAction(alwaysAdjustAction);
    menu.addSeparator();
    menu.addAction(debuggerCore()->action(SettingsDialog));

    QAction *act = menu.exec(ev->globalPos());

    if (act == deleteAction)
        deleteBreakpoints(selectedIds);
    else if (act == deleteAllAction)
        deleteBreakpoints(handler->allBreakpointIds());
    else if (act == deleteByFileAction)
        deleteBreakpoints(breakpointsInFile);
    else if (act == adjustColumnAction)
        resizeColumnsToContents();
    else if (act == alwaysAdjustAction)
        setAlwaysResizeColumnsToContents(!m_alwaysResizeColumnsToContents);
    else if (act == editBreakpointAction)
        editBreakpoints(selectedIds);
    else if (act == associateBreakpointAction)
        associateBreakpoint(selectedIds, threadId);
    else if (act == synchronizeAction)
        ; //synchronizeBreakpoints();
    else if (act == toggleEnabledAction)
        setBreakpointsEnabled(selectedIds, !enabled);
    else if (act == addBreakpointAction)
        addBreakpoint();
    else if (act == breakAtThrowAction)
        handler->appendBreakpoint(BreakpointParameters(BreakpointAtThrow));
    else if (act == breakAtCatchAction)
        handler->appendBreakpoint(BreakpointParameters(BreakpointAtCatch));
}

void BreakWindow::setBreakpointsEnabled(const BreakpointIds &ids, bool enabled)
{
    BreakHandler *handler = breakHandler();
    foreach (const BreakpointId id, ids)
        handler->setEnabled(id, enabled);
}

void BreakWindow::deleteBreakpoints(const BreakpointIds &ids)
{
    BreakHandler *handler = breakHandler();
    foreach (const BreakpointId id, ids)
       handler->removeBreakpoint(id);
}

void BreakWindow::editBreakpoint(BreakpointId id, QWidget *parent)
{
    BreakpointDialog dialog(parent);
    BreakpointParameters data = breakHandler()->breakpointData(id);
    if (dialog.showDialog(&data))
        breakHandler()->setBreakpointData(id, data);
}

void BreakWindow::addBreakpoint()
{
    BreakpointParameters data(BreakpointByFileAndLine);
    BreakpointDialog dialog(this);
    if (dialog.showDialog(&data))
        breakHandler()->appendBreakpoint(data);
}

void BreakWindow::editBreakpoints(const BreakpointIds &ids)
{
    QTC_ASSERT(!ids.isEmpty(), return);

    const BreakpointId id = ids.at(0);

    if (ids.size() == 1) {
        editBreakpoint(id, this);
        return;
    }

    // This allows to change properties of multiple breakpoints at a time.
    QDialog dlg(this);
    Ui::BreakCondition ui;
    ui.setupUi(&dlg);
    dlg.setWindowTitle(tr("Edit Breakpoint Properties"));
    ui.lineEditIgnoreCount->setValidator(
        new QIntValidator(0, 2147483647, ui.lineEditIgnoreCount));

    BreakHandler *handler = breakHandler();
    const QString oldCondition = QString::fromLatin1(handler->condition(id));
    const QString oldIgnoreCount = QString::number(handler->ignoreCount(id));
    const QString oldThreadSpec = QString::fromLatin1(handler->threadSpec(id));

    ui.lineEditCondition->setText(oldCondition);
    ui.lineEditIgnoreCount->setText(oldIgnoreCount);
    ui.lineEditThreadSpec->setText(oldThreadSpec);

    if (dlg.exec() == QDialog::Rejected)
        return;

    const QString newCondition = ui.lineEditCondition->text();
    const QString newIgnoreCount = ui.lineEditIgnoreCount->text();
    const QString newThreadSpec = ui.lineEditThreadSpec->text();

    if (newCondition == oldCondition && newIgnoreCount == oldIgnoreCount
            && newThreadSpec == oldThreadSpec)
        return;

    foreach (const BreakpointId id, ids) {
        handler->setCondition(id, newCondition.toLatin1());
        handler->setIgnoreCount(id, newIgnoreCount.toInt());
        handler->setThreadSpec(id, newThreadSpec.toLatin1());
    }
}

void BreakWindow::associateBreakpoint(const BreakpointIds &ids, int threadId)
{
    BreakHandler *handler = breakHandler();
    QByteArray spec = QByteArray::number(threadId);
    foreach (const BreakpointId id, ids)
        handler->setThreadSpec(id, spec);
}

void BreakWindow::resizeColumnsToContents()
{
    for (int i = model()->columnCount(); --i >= 0; )
        resizeColumnToContents(i);
}

void BreakWindow::setAlwaysResizeColumnsToContents(bool on)
{
    m_alwaysResizeColumnsToContents = on;
    QHeaderView::ResizeMode mode = on
        ? QHeaderView::ResizeToContents : QHeaderView::Interactive;
    for (int i = model()->columnCount(); --i >= 0; )
        header()->setResizeMode(i, mode);
}

void BreakWindow::rowActivated(const QModelIndex &index)
{
    breakHandler()->gotoLocation(breakHandler()->findBreakpointByIndex(index));
}

} // namespace Internal
} // namespace Debugger

#include "breakwindow.moc"
