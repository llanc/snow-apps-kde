#include "app/application_setup.h"
#include "ui/viewer_window.h"

#include <QApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <cstdio>

int main(int argc, char** argv) {
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("XDG_DATA_HOME", directory.path().toUtf8());
    qputenv("XDG_CACHE_HOME", directory.path().toUtf8());
    QApplication app(argc, argv);
    snow::image_viewer::configureViewerApplicationIdentity(app);
    snow::image_viewer::configureViewerApplicationAppearance(app);

    const QString imagePath = directory.filePath(QStringLiteral("fixture.png"));
    QImage image(128, 64, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    if (!image.save(imagePath))
        return 1;
    snow::image_viewer::ViewerWindow window;
    window.openImage(imagePath);
    window.show();
    const bool loaded = QTest::qWaitFor(
        [&] {
            return window.windowTitle().contains(QStringLiteral("fixture.png")) &&
                   window.windowTitle().contains(QStringLiteral("128 \u00d7 64"));
        },
        10000);
    if (!loaded) {
        std::fprintf(stderr, "The viewer did not load the synthetic PNG: %s\n",
                     qPrintable(window.windowTitle()));
        return 1;
    }
    return 0;
}
