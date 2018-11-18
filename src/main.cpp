/*
 * Copyright © 2015-2016 Antti Lamminsalo
 *
 * This file is part of Orion.
 *
 * Orion is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with Orion.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QQmlApplicationEngine>
#include <QScreen>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QNetworkProxyFactory>
#include <QFontDatabase>
#include <QIcon>
#include <QQuickWindow>
#include <QLockFile>

#include "model/channelmanager.h"
#include "network/networkmanager.h"
#include "model/vodmanager.h"
#include "model/ircchat.h"
#include "network/httpserver.h"
#include "model/viewersmodel.h"
#include "power/power.h"

#ifndef Q_OS_ANDROID
#include <QApplication>
#ifndef Q_OS_WIN
#include "notification/notificationmanager.h"
#endif
#else
#include <QGuiApplication>
#endif

#ifdef MPV_PLAYER
#include "player/mpvobject.h"
#endif


template <unsigned int MIN_LEVEL=QtDebugMsg>
void msgHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (static_cast<unsigned int>(type) < MIN_LEVEL) {
        return;
    }
    QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        fprintf(stdout, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtInfoMsg:
        fprintf(stdout, "Info: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    }
}


void registerQmlComponents(QObject *parent)
{
    qmlRegisterSingletonType<ChannelManager>("app.orion", 1, 0, "ChannelManager", &ChannelManager::provider);
    qmlRegisterSingletonType<BadgeContainer>("app.orion", 1, 0, "Emotes", &BadgeContainer::provider);
    qmlRegisterSingletonType<ViewersModel>("app.orion", 1, 0, "Viewers", &ViewersModel::provider);
    qmlRegisterSingletonType<VodManager>("app.orion", 1, 0, "VodManager", &VodManager::provider);
    qmlRegisterSingletonType<SettingsManager>("app.orion", 1, 0, "Settings", &SettingsManager::provider);
    qmlRegisterSingletonType<HttpServer>("app.orion", 1, 0, "LoginService", &HttpServer::provider);
    qmlRegisterSingletonType<NetworkManager>("app.orion", 1, 0, "Network", &NetworkManager::provider);
    qmlRegisterSingletonType<Power>("app.orion", 1, 0, "PowerManager", &Power::provider);
    qmlRegisterType<IrcChat>("aldrog.twitchtube.ircchat", 1, 0, "IrcChat");

#ifdef MPV_PLAYER
    qmlRegisterType<MpvObject>("mpv", 1, 0, "MpvObject");
#endif

    //Setup obj parents for cleanup
    ChannelManager::getInstance()->setParent(parent);
    BadgeContainer::getInstance()->setParent(parent);
    ViewersModel::getInstance()->setParent(parent);
    VodManager::getInstance()->setParent(parent);
    SettingsManager::getInstance()->setParent(parent);
    HttpServer::getInstance()->setParent(parent);
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(&msgHandler<QtWarningMsg>);
    QCoreApplication::setApplicationName("Orion");
    QCoreApplication::setOrganizationName("orion.application");
    QCoreApplication::setApplicationVersion(APP_VERSION);

    //Override QT_QUICK_CONTROLS_STYLE environment variable
    qputenv("QT_QUICK_CONTROLS_STYLE", "material");

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    auto opengl = SettingsManager::getInstance()->opengl().toLower();

    // OpenGL implementation used to render app.
    // Need to be set before constructing QGuiApplication
    // http://doc.qt.io/qt-5/qt.html#ApplicationAttribute-enum
    if (opengl.contains("desktop")) {
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    } else if(opengl.contains("software")) {
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    } else {
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
#ifdef QT_OPENGL_DYNAMIC
        qputenv("QT_OPENGL", "angle");
#endif
#ifdef Q_OS_WIN
        if (opengl.contains("d3d11"))
            qputenv("QT_ANGLE_PLATFORM", "d3d11");
        else if (opengl.contains("d3d9"))
            qputenv("QT_ANGLE_PLATFORM", "d3d9");
        else if (opengl.contains("warp"))
            qputenv("QT_ANGLE_PLATFORM", "warp");
#endif
    }

#ifndef Q_OS_ANDROID
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif

    const QIcon appIcon = QIcon(":/icon/orion.ico");
    app.setWindowIcon(appIcon);

    QQmlApplicationEngine engine;

    //Prime network manager
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    NetworkManager::initialize(engine.networkAccessManager());

#ifndef Q_OS_ANDROID
    QCommandLineParser parser;
    parser.setApplicationDescription("Twitch.tv client");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption debugOption(QStringList() << "d" << "debug", "show debug output");
    parser.addOption(debugOption);

    QCommandLineOption quietOption(QStringList() << "q" << "quiet", "disable console output");
    parser.addOption(quietOption);

    parser.process(QCoreApplication::arguments());

    if (parser.isSet(quietOption)) {
        qInstallMessageHandler(&msgHandler<QtSystemMsg+1>);
    } else if (parser.isSet(debugOption)) {
        qInstallMessageHandler(&msgHandler<QtDebugMsg>);
    }
#endif

    // detect hi dpi screens
    qDebug() << "Screens:";
    int screens = 0;
    qreal maxDevicePixelRatio = QGuiApplication::primaryScreen()->devicePixelRatio();
    for (const auto & screen : QGuiApplication::screens()) {
        qreal curPixelRatio = screen->devicePixelRatio();
        maxDevicePixelRatio = qMax(maxDevicePixelRatio, curPixelRatio);
        screens++;
        qDebug() << "  Screen #" << screens << screen->name() << ": devicePixelRatio" << curPixelRatio;
    }
    qDebug() << "maxDevicePixelRatio" << maxDevicePixelRatio;

    SettingsManager::getInstance()->setHiDpi(maxDevicePixelRatio > 1.0);

#ifndef Q_OS_WIN
    //Set up notifications
    NotificationManager *notificationManager = new NotificationManager(&engine, engine.networkAccessManager(), &app);
    QObject::connect(ChannelManager::getInstance(), &ChannelManager::pushNotification, notificationManager, &NotificationManager::pushNotification);
#endif

    QQmlContext *rootContext = engine.rootContext();
    rootContext->setContextProperty("g_favourites", ChannelManager::getInstance()->getFavouritesProxy());
    rootContext->setContextProperty("g_results", ChannelManager::getInstance()->getResultsModel());
    rootContext->setContextProperty("g_games", ChannelManager::getInstance()->getGamesModel());
    rootContext->setContextProperty("vodsModel", VodManager::getInstance()->getModel());


    rootContext->setContextProperty("g_instance", "main");

#ifndef Q_OS_ANDROID
    //Single application solution
    QLockFile lockfile(QDir::temp().absoluteFilePath("wz0dPKqHv3vX0BBsUFZt.lock"));
    if (!lockfile.tryLock(100)) {
        rootContext->setContextProperty("g_instance", "child");
    }
#endif

    // Register qml components
    registerQmlComponents(&app);

    // Load QML content
    engine.load(QUrl("qrc:/main.qml"));
#ifndef Q_OS_ANDROID
    // Get QML root window, add connections
    QQuickWindow *rootWin = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!rootWin) {
        qFatal("Main window was not opened.");
        return -1;
    }
#endif

    // first check
    ChannelManager::getInstance()->checkFavourites();

    // Start
    return app.exec();
}
