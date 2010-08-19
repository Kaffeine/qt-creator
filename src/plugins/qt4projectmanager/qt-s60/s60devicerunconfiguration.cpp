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

#include "s60devicerunconfiguration.h"
#include "s60devicerunconfigurationwidget.h"
#include "s60deployconfiguration.h"
#include "qt4project.h"
#include "qt4target.h"
#include "qtversionmanager.h"
#include "profilereader.h"
#include "s60manager.h"
#include "s60devices.h"
#include "s60runconfigbluetoothstarter.h"
#include "bluetoothlistener_gui.h"
#include "symbiandevicemanager.h"
#include "qt4buildconfiguration.h"
#include "qt4projectmanagerconstants.h"
#include "s60createpackagestep.h"
#include "qtoutputformatter.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <utils/qtcassert.h>
#include <utils/pathchooser.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/project.h>

#include <debugger/debuggerengine.h>
#include <debugger/debuggerplugin.h>
#include <debugger/debuggerrunner.h>

#include <QtGui/QMessageBox>
#include <QtGui/QMainWindow>
#include <QtCore/QCoreApplication>

using namespace ProjectExplorer;
using namespace Qt4ProjectManager;
using namespace Qt4ProjectManager::Internal;

namespace {
const char * const S60_DEVICE_RC_ID("Qt4ProjectManager.S60DeviceRunConfiguration");
const char * const S60_DEVICE_RC_PREFIX("Qt4ProjectManager.S60DeviceRunConfiguration.");

const char * const PRO_FILE_KEY("Qt4ProjectManager.S60DeviceRunConfiguration.ProFile");
const char * const COMMUNICATION_TYPE_KEY("Qt4ProjectManager.S60DeviceRunConfiguration.CommunicationType");
const char * const COMMAND_LINE_ARGUMENTS_KEY("Qt4ProjectManager.S60DeviceRunConfiguration.CommandLineArguments");

const int    PROGRESS_MAX = 200;

enum { debug = 0 };

// Format information about a file
static inline QString msgListFile(const QString &f)
{
    QString rc;
    const QFileInfo fi(f);
    QTextStream str(&rc);
    if (fi.exists()) {
        str << fi.size() << ' ' << fi.lastModified().toString(Qt::ISODate) << ' ' << QDir::toNativeSeparators(fi.absoluteFilePath());
    } else {
        str << "<non-existent> " << QDir::toNativeSeparators(fi.absoluteFilePath());
    }
    return rc;
}

QString pathFromId(const QString &id)
{
    if (!id.startsWith(QLatin1String(S60_DEVICE_RC_PREFIX)))
        return QString();
    return id.mid(QString::fromLatin1(S60_DEVICE_RC_PREFIX).size());
}

QString pathToId(const QString &path)
{
    return QString::fromLatin1(S60_DEVICE_RC_PREFIX) + path;
}

}

// ======== S60DeviceRunConfiguration

S60DeviceRunConfiguration::S60DeviceRunConfiguration(Target *parent, const QString &proFilePath) :
    RunConfiguration(parent,  QLatin1String(S60_DEVICE_RC_ID)),
    m_proFilePath(proFilePath)
{
    ctor();
}

S60DeviceRunConfiguration::S60DeviceRunConfiguration(Target *target, S60DeviceRunConfiguration *source) :
    RunConfiguration(target, source),
    m_proFilePath(source->m_proFilePath),
    m_commandLineArguments(source->m_commandLineArguments)
{
    ctor();
}

void S60DeviceRunConfiguration::ctor()
{
    if (!m_proFilePath.isEmpty())
        setDisplayName(tr("%1 on Symbian Device").arg(QFileInfo(m_proFilePath).completeBaseName()));
    else
        setDisplayName(tr("QtS60DeviceRunConfiguration"));
}

void S60DeviceRunConfiguration::proFileUpdate(Qt4ProjectManager::Internal::Qt4ProFileNode *pro)
{
    if (m_proFilePath == pro->path())
        emit targetInformationChanged();
}

S60DeviceRunConfiguration::~S60DeviceRunConfiguration()
{
}

Qt4Target *S60DeviceRunConfiguration::qt4Target() const
{
    return static_cast<Qt4Target *>(target());
}

ProjectExplorer::ToolChain::ToolChainType S60DeviceRunConfiguration::toolChainType(
        ProjectExplorer::BuildConfiguration *configuration) const
{
    if (Qt4BuildConfiguration *bc = qobject_cast<Qt4BuildConfiguration *>(configuration))
        return bc->toolChainType();
    return ProjectExplorer::ToolChain::INVALID;
}

ProjectExplorer::ToolChain::ToolChainType S60DeviceRunConfiguration::toolChainType() const
{
    if (Qt4BuildConfiguration *bc = qobject_cast<Qt4BuildConfiguration *>(target()->activeBuildConfiguration()))
        return bc->toolChainType();
    return ProjectExplorer::ToolChain::INVALID;
}

bool S60DeviceRunConfiguration::isEnabled(ProjectExplorer::BuildConfiguration *configuration) const
{
    const Qt4BuildConfiguration *qt4bc = static_cast<const Qt4BuildConfiguration *>(configuration);
    switch (qt4bc->toolChainType()) {
    case ToolChain::GCCE:
    case ToolChain::RVCT_ARMV5:
    case ToolChain::RVCT_ARMV6:
    case ToolChain::GCCE_GNUPOC:
    case ToolChain::RVCT_ARMV5_GNUPOC:
        return true;
    default:
        break;
    }
    return false;
}

QWidget *S60DeviceRunConfiguration::createConfigurationWidget()
{
    return new S60DeviceRunConfigurationWidget(this);
}

ProjectExplorer::OutputFormatter *S60DeviceRunConfiguration::createOutputFormatter() const
{
    return new QtOutputFormatter(qt4Target()->qt4Project());
}

QVariantMap S60DeviceRunConfiguration::toMap() const
{
    QVariantMap map(ProjectExplorer::RunConfiguration::toMap());
    const QDir projectDir = QDir(target()->project()->projectDirectory());

    map.insert(QLatin1String(PRO_FILE_KEY), projectDir.relativeFilePath(m_proFilePath));
    map.insert(QLatin1String(COMMAND_LINE_ARGUMENTS_KEY), m_commandLineArguments);

    return map;
}

bool S60DeviceRunConfiguration::fromMap(const QVariantMap &map)
{
    const QDir projectDir = QDir(target()->project()->projectDirectory());

    m_proFilePath = projectDir.filePath(map.value(QLatin1String(PRO_FILE_KEY)).toString());
    m_commandLineArguments = map.value(QLatin1String(COMMAND_LINE_ARGUMENTS_KEY)).toStringList();

    return RunConfiguration::fromMap(map);
}

static inline QString fixBaseNameTarget(const QString &in)
{
    if (in == QLatin1String("udeb"))
        return QLatin1String("debug");
    if (in == QLatin1String("urel"))
        return QLatin1String("release");
    return in;
}

QString S60DeviceRunConfiguration::projectFilePath() const
{
    return m_proFilePath;
}

QStringList S60DeviceRunConfiguration::commandLineArguments() const
{
    return m_commandLineArguments;
}

void S60DeviceRunConfiguration::setCommandLineArguments(const QStringList &args)
{
    m_commandLineArguments = args;
}

// ======== S60DeviceRunConfigurationFactory

S60DeviceRunConfigurationFactory::S60DeviceRunConfigurationFactory(QObject *parent) :
    IRunConfigurationFactory(parent)
{
}

S60DeviceRunConfigurationFactory::~S60DeviceRunConfigurationFactory()
{
}

QStringList S60DeviceRunConfigurationFactory::availableCreationIds(Target *parent) const
{
    Qt4Target *target = qobject_cast<Qt4Target *>(parent);
    if (!target ||
        target->id() != QLatin1String(Constants::S60_DEVICE_TARGET_ID))
        return QStringList();

    return target->qt4Project()->applicationProFilePathes(QLatin1String(S60_DEVICE_RC_PREFIX));
}

QString S60DeviceRunConfigurationFactory::displayNameForId(const QString &id) const
{
    if (!pathFromId(id).isEmpty())
        return tr("%1 on Symbian Device").arg(QFileInfo(pathFromId(id)).completeBaseName());
    return QString();
}

bool S60DeviceRunConfigurationFactory::canCreate(Target *parent, const QString &id) const
{
    Qt4Target * t(qobject_cast<Qt4Target *>(parent));
    if (!t ||
        t->id() != QLatin1String(Constants::S60_DEVICE_TARGET_ID))
        return false;
    return t->qt4Project()->hasApplicationProFile(pathFromId(id));
}

RunConfiguration *S60DeviceRunConfigurationFactory::create(Target *parent, const QString &id)
{
    if (!canCreate(parent, id))
        return 0;

    Qt4Target *t(static_cast<Qt4Target *>(parent));
    return new S60DeviceRunConfiguration(t, pathFromId(id));
}

bool S60DeviceRunConfigurationFactory::canRestore(Target *parent, const QVariantMap &map) const
{
    Qt4Target * t(qobject_cast<Qt4Target *>(parent));
    if (!t ||
        t->id() != QLatin1String(Constants::S60_DEVICE_TARGET_ID))
        return false;
    QString id(ProjectExplorer::idFromMap(map));
    return id == QLatin1String(S60_DEVICE_RC_ID);
}

RunConfiguration *S60DeviceRunConfigurationFactory::restore(Target *parent, const QVariantMap &map)
{
    if (!canRestore(parent, map))
        return 0;
    Qt4Target *t(static_cast<Qt4Target *>(parent));
    S60DeviceRunConfiguration *rc(new S60DeviceRunConfiguration(t, QString()));
    if (rc->fromMap(map))
        return rc;

    delete rc;
    return 0;
}

bool S60DeviceRunConfigurationFactory::canClone(Target *parent, RunConfiguration *source) const
{
    if (!qobject_cast<Qt4Target *>(parent))
        return false;
    return source->id() == QLatin1String(S60_DEVICE_RC_ID);
}

RunConfiguration *S60DeviceRunConfigurationFactory::clone(Target *parent, RunConfiguration *source)
{
    if (!canClone(parent, source))
        return 0;
    Qt4Target *t = static_cast<Qt4Target *>(parent);
    S60DeviceRunConfiguration * old(static_cast<S60DeviceRunConfiguration *>(source));
    return new S60DeviceRunConfiguration(t, old);
}

// ======== S60DeviceRunControlBase

S60DeviceRunControl::S60DeviceRunControl(RunConfiguration *runConfiguration, QString mode) :
    RunControl(runConfiguration, mode),
    m_toolChain(ProjectExplorer::ToolChain::INVALID),
    m_handleDeviceRemoval(true),
    m_launcher(0)
{
    // connect for automatically reporting the "finished deploy" state to the progress manager
    connect(this, SIGNAL(finished()), this, SLOT(reportDeployFinished()));

    S60DeviceRunConfiguration *s60runConfig = qobject_cast<S60DeviceRunConfiguration *>(runConfiguration);
    const Qt4BuildConfiguration *activeBuildConf = s60runConfig->qt4Target()->activeBuildConfiguration();
    S60DeployConfiguration *activeDeployConf = qobject_cast<S60DeployConfiguration *>(s60runConfig->qt4Target()->activeDeployConfiguration());

    QTC_ASSERT(s60runConfig, return);
    m_toolChain = s60runConfig->toolChainType();
    m_serialPortName = activeDeployConf->serialPortName();
    m_serialPortFriendlyName = SymbianUtils::SymbianDeviceManager::instance()->friendlyNameForPort(m_serialPortName);
    m_targetName = activeDeployConf->targetName();
    m_commandLineArguments = s60runConfig->commandLineArguments();
    m_qtDir = activeBuildConf->qtVersion()->versionInfo().value("QT_INSTALL_DATA");
    m_installationDrive = activeDeployConf->installationDrive();
    if (const QtVersion *qtv = activeDeployConf->qtVersion())
        m_qtBinPath = qtv->versionInfo().value(QLatin1String("QT_INSTALL_BINS"));
    QTC_ASSERT(!m_qtBinPath.isEmpty(), return);
    m_executableFileName = activeDeployConf->localExecutableFileName();
    if (debug)
        qDebug() << "S60DeviceRunControl::CT" << m_targetName << ProjectExplorer::ToolChain::toolChainName(m_toolChain)
                 << m_serialPortName;
}

S60DeviceRunControl::~S60DeviceRunControl()
{
    if (m_launcher) {
        m_launcher->deleteLater();
        m_launcher = 0;
    }
}

void S60DeviceRunControl::start()
{
    m_launchProgress = new QFutureInterface<void>;
    Core::ICore::instance()->progressManager()->addTask(m_launchProgress->future(),
                                                        tr("Launching"),
                                                        QLatin1String("Symbian.Launch"));
    m_launchProgress->setProgressRange(0, PROGRESS_MAX);
    m_launchProgress->setProgressValue(0);
    m_launchProgress->reportStarted();
    emit started();
    if (m_serialPortName.isEmpty()) {
        m_launchProgress->reportCanceled();
        appendMessage(this, tr("There is no device plugged in."), true);
        emit finished();
        return;
    }

    emit appendMessage(this, tr("Executable file: %1").arg(msgListFile(m_executableFileName)), false);

    QString errorMessage;
    QString settingsCategory;
    QString settingsPage;
    if (!checkConfiguration(&errorMessage, &settingsCategory, &settingsPage)) {
        m_launchProgress->reportCanceled();
        appendMessage(this, errorMessage, true);
        emit finished();
        Core::ICore::instance()->showWarningWithOptions(tr("Debugger for Symbian Platform"),
                                                        errorMessage, QString(),
                                                        settingsCategory, settingsPage);
        return;
    }

    startLaunching();
}

void S60DeviceRunControl::stop()
{
    if (m_launcher)
        m_launcher->terminate();
}

bool S60DeviceRunControl::isRunning() const
{
    return m_launcher && (m_launcher->state() == trk::Launcher::Connecting
                          || m_launcher->state() == trk::Launcher::Connected
                          || m_launcher->state() == trk::Launcher::WaitingForTrk);
}

void S60DeviceRunControl::startLaunching()
{
    QString errorMessage;
    if (setupLauncher(errorMessage)) {
        if (m_launchProgress)
                    m_launchProgress->setProgressValue(PROGRESS_MAX/2);
    } else {
        if (!errorMessage.isEmpty())
            appendMessage(this, errorMessage, true);
        stop();
        emit finished();
    }
}

bool S60DeviceRunControl::setupLauncher(QString &errorMessage)
{
    connect(SymbianUtils::SymbianDeviceManager::instance(), SIGNAL(deviceRemoved(const SymbianUtils::SymbianDevice)),
            this, SLOT(deviceRemoved(SymbianUtils::SymbianDevice)));
    m_launcher = trk::Launcher::acquireFromDeviceManager(m_serialPortName, 0, &errorMessage);
    if (!m_launcher)
        return false;

    connect(m_launcher, SIGNAL(finished()), this, SLOT(launcherFinished()));
    connect(m_launcher, SIGNAL(canNotConnect(QString)), this, SLOT(printConnectFailed(QString)));
    connect(m_launcher, SIGNAL(stateChanged(int)), this, SLOT(slotLauncherStateChanged(int)));
    connect(m_launcher, SIGNAL(processStopped(uint,uint,uint,QString)),
            this, SLOT(processStopped(uint,uint,uint,QString)));

    if (!m_commandLineArguments.isEmpty())
        m_launcher->setCommandLineArgs(m_commandLineArguments);

    const QString runFileName = QString::fromLatin1("%1:\\sys\\bin\\%2.exe").arg(m_installationDrive).arg(m_targetName);
    initLauncher(runFileName, m_launcher);
    const trk::PromptStartCommunicationResult src =
            S60RunConfigBluetoothStarter::startCommunication(m_launcher->trkDevice(),
                                                             0, &errorMessage);
    if (src != trk::PromptStartCommunicationConnected)
        return false;

    if (!m_launcher->startServer(&errorMessage)) {
        errorMessage = tr("Could not connect to phone on port '%1': %2\n"
                          "Check if the phone is connected and App TRK is running.").arg(m_serialPortName, errorMessage);
        return false;
    }
    return true;
}

void S60DeviceRunControl::printConnectFailed(const QString &errorMessage)
{
    emit appendMessage(this, tr("Could not connect to App TRK on device: %1. Restarting App TRK might help.").arg(errorMessage), true);
}

void S60DeviceRunControl::launcherFinished()
{
    m_handleDeviceRemoval = false;
    trk::Launcher::releaseToDeviceManager(m_launcher);
    m_launcher->deleteLater();
    m_launcher = 0;
    handleLauncherFinished();
}

void S60DeviceRunControl::reportDeployFinished()
{
    if (m_launchProgress) {
        m_launchProgress->reportFinished();
        delete m_launchProgress;
        m_launchProgress = 0;
    }
}

void S60DeviceRunControl::processStopped(uint pc, uint pid, uint tid, const QString& reason)
{
    emit addToOutputWindow(this, trk::Launcher::msgStopped(pid, tid, pc, reason), false);
    m_launcher->terminate();
}

QMessageBox *S60DeviceRunControl::createTrkWaitingMessageBox(const QString &port, QWidget *parent)
{
    const QString title  = QCoreApplication::translate("Qt4ProjectManager::Internal::S60DeviceRunControlBase",
                                                       "Waiting for App TRK");
    const QString text = QCoreApplication::translate("Qt4ProjectManager::Internal::S60DeviceRunControlBase",
                                                     "Qt creator waiting for the TRK application.<br>"
                                                     "Please make sure the application is running on "
                                                     "your mobile phone and the right port is "
                                                     "configured in the project settings.").arg(port);
    QMessageBox *rc = new QMessageBox(QMessageBox::Information, title, text,
                                      QMessageBox::Cancel, parent);
    return rc;
}

void S60DeviceRunControl::slotLauncherStateChanged(int s)
{
    if (s == trk::Launcher::WaitingForTrk) {
        QMessageBox *mb = S60DeviceRunControl::createTrkWaitingMessageBox(m_launcher->trkServerName(),
                                                     Core::ICore::instance()->mainWindow());
        connect(m_launcher, SIGNAL(stateChanged(int)), mb, SLOT(close()));
        connect(mb, SIGNAL(finished(int)), this, SLOT(slotWaitingForTrkClosed()));
        mb->open();
    }
}

void S60DeviceRunControl::slotWaitingForTrkClosed()
{
    if (m_launcher && m_launcher->state() == trk::Launcher::WaitingForTrk) {
        stop();
        appendMessage(this, tr("Canceled."), true);
        emit finished();
    }
}

void S60DeviceRunControl::printApplicationOutput(const QString &output)
{
    printApplicationOutput(output, false);
}

void S60DeviceRunControl::printApplicationOutput(const QString &output, bool onStdErr)
{
    emit addToOutputWindowInline(this, output, onStdErr);
}

void S60DeviceRunControl::deviceRemoved(const SymbianUtils::SymbianDevice &d)
{
    if (m_handleDeviceRemoval && d.portName() == m_serialPortName) {
        appendMessage(this, tr("The device '%1' has been disconnected").arg(d.friendlyName()), true);
        emit finished();
    }
}

bool S60DeviceRunControl::checkConfiguration(QString * /* errorMessage */,
                                                 QString * /* settingsCategory */,
                                                 QString * /* settingsPage */) const
{
    return true;
}

void S60DeviceRunControl::initLauncher(const QString &executable, trk::Launcher *launcher)
{
     connect(launcher, SIGNAL(startingApplication()), this, SLOT(printStartingNotice()));
     connect(launcher, SIGNAL(applicationRunning(uint)), this, SLOT(applicationRunNotice(uint)));
     connect(launcher, SIGNAL(canNotRun(QString)), this, SLOT(applicationRunFailedNotice(QString)));
     connect(launcher, SIGNAL(applicationOutputReceived(QString)), this, SLOT(printApplicationOutput(QString)));
     launcher->addStartupActions(trk::Launcher::ActionRun);
     launcher->setFileName(executable);
}

void S60DeviceRunControl::handleLauncherFinished()
{
     emit finished();
     emit appendMessage(this, tr("Finished."), false);
 }

void S60DeviceRunControl::printStartingNotice()
{
    emit appendMessage(this, tr("Starting application..."), false);
}

void S60DeviceRunControl::applicationRunNotice(uint pid)
{
    emit appendMessage(this, tr("Application running with pid %1.").arg(pid), false);
    if (m_launchProgress)
        m_launchProgress->setProgressValue(PROGRESS_MAX);
}

void S60DeviceRunControl::applicationRunFailedNotice(const QString &errorMessage)
{
    emit appendMessage(this, tr("Could not start application: %1").arg(errorMessage), true);
}

// ======== S60DeviceDebugRunControl

S60DeviceDebugRunControl::S60DeviceDebugRunControl(S60DeviceRunConfiguration *rc, QString mode) :
    RunControl(rc, mode),
    m_startParams(new Debugger::DebuggerStartParameters),
    m_debuggerRunControl(0),
    m_debugProgress(0)
{
    QTC_ASSERT(rc, return);

    S60DeployConfiguration *activeDeployConf = qobject_cast<S60DeployConfiguration *>(rc->qt4Target()->activeDeployConfiguration());

    const QString debugFileName = QString::fromLatin1("%1:\\sys\\bin\\%2.exe")
            .arg(activeDeployConf->installationDrive()).arg(activeDeployConf->targetName());

    m_startParams->remoteChannel = activeDeployConf->serialPortName();
    m_startParams->processArgs = rc->commandLineArguments();
    m_startParams->startMode = Debugger::StartInternal;
    m_startParams->toolChainType = rc->toolChainType();
    m_startParams->executable = debugFileName;

    // Prefer the '*.sym' file over the '.exe', which should exist at the same
    // location in debug builds

    if (!QFileInfo(m_startParams->symbolFileName).isFile()) {
        m_startParams->symbolFileName.clear();
        emit appendMessage(this, tr("Warning: Cannot locate the symbol file belonging to %1.").arg(m_localExecutableFileName), true);
    }

    m_localExecutableFileName = activeDeployConf->localExecutableFileName();
    const int lastDotPos = m_localExecutableFileName.lastIndexOf(QLatin1Char('.'));
    if (lastDotPos != -1) {
        m_startParams->symbolFileName = m_localExecutableFileName.mid(0, lastDotPos) + QLatin1String(".sym");
    }
}

void S60DeviceDebugRunControl::start()
{
    m_debugProgress = new QFutureInterface<void>;
    Core::ICore::instance()->progressManager()->addTask(m_debugProgress->future(),
                                                        tr("Debugging"),
                                                        QLatin1String("Symbian.Debug"));
    m_debugProgress->setProgressRange(0, PROGRESS_MAX);
    m_debugProgress->setProgressValue(0);
    m_debugProgress->reportStarted();
    emit started();

    QString errorMessage;
    QString settingsCategory;
    QString settingsPage;
    if (!checkConfiguration(&errorMessage, &settingsCategory, &settingsPage)) {
        m_debugProgress->reportCanceled();
        appendMessage(this, errorMessage, true);
        emit finished();
        Core::ICore::instance()->showWarningWithOptions(tr("Debugger for Symbian Platform"),
                                                        errorMessage, QString(),
                                                        settingsCategory, settingsPage);
        return;
    }

    using namespace Debugger;
    emit appendMessage(this, tr("Launching debugger..."), false);
    QTC_ASSERT(m_debuggerRunControl == 0, /* Should happen only once. */);
    m_debuggerRunControl = DebuggerPlugin::createDebugger(*m_startParams.data());

    connect(m_debuggerRunControl,
            SIGNAL(finished()),
            SLOT(debuggingFinished()),
            Qt::QueuedConnection);
    connect(m_debuggerRunControl,
            SIGNAL(addToOutputWindowInline(ProjectExplorer::RunControl*,QString,bool)),
            SIGNAL(addToOutputWindowInline(ProjectExplorer::RunControl*,QString,bool)),
            Qt::QueuedConnection);

    DebuggerPlugin::startDebugger(m_debuggerRunControl);
}


S60DeviceDebugRunControl::~S60DeviceDebugRunControl()
{
    delete m_debugProgress;
    // FIXME: Needed? m_debuggerRunControl->deleteLater();
}

void S60DeviceDebugRunControl::stop()
{
    QTC_ASSERT(m_debuggerRunControl, return)
    if (m_debuggerRunControl->state() == Debugger::DebuggerNotReady)
        m_debuggerRunControl->stop();
    if (m_debugProgress)
        m_debugProgress->reportCanceled();
    delete m_debugProgress;
    m_debugProgress = 0;
}

bool S60DeviceDebugRunControl::isRunning() const
{
    return m_debuggerRunControl
            && m_debuggerRunControl->isRunning();
}

void S60DeviceDebugRunControl::debuggingFinished()
{
    emit appendMessage(this, tr("Debugging finished."), false);
    emit finished();
    if (m_debugProgress) {
        m_debugProgress->setProgressValue(PROGRESS_MAX);
        m_debugProgress->reportFinished();
    }
    delete m_debugProgress;
    m_debugProgress = 0;
}

bool S60DeviceDebugRunControl::checkConfiguration(QString *errorMessage,
                                                  QString *settingsCategory,
                                                  QString *settingsPage) const
{
    return Debugger::DebuggerRunControl::checkDebugConfiguration(
                                                m_startParams->toolChainType,
                                                errorMessage,
                                                settingsCategory,
                                                settingsPage);
}
