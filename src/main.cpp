#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "systemtheme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omasign"));
    app.setDesktopFileName(QStringLiteral("omasign"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omasign")));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend, &Backend::setDarkMode);

    const QFont baseFont = app.font();
    const auto applyTextScale = [&app, &backend, baseFont](qreal textScale) {
        QFont scaled = baseFont;
        scaled.setPointSizeF(baseFont.pointSizeF() * textScale);
        app.setFont(scaled);
        backend.setTextScale(textScale);
    };
    applyTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend, applyTextScale);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.addImageProvider(QStringLiteral("pdf"), new PageImageProvider(&backend));

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omasign interface; resource available:"
                    << QFile::exists(QStringLiteral(":/Main.qml"));
        return -1;
    }

    const QStringList args = app.arguments();
    if (args.size() > 1)
        backend.open(QUrl::fromLocalFile(args.at(1)));

    return app.exec();
}
