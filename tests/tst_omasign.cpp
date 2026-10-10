#include <QtTest>

#include <QElapsedTimer>
#include <QPointingDevice>
#include <QWheelEvent>
#include <QJSValue>
#include <QPageSize>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <poppler-annotation.h>
#include <poppler-qt6.h>

#include "backend.h"

class TestOmasign : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void saveWritesAnnotations();
    void addSignatureKeysOutPaper();
    void uiPlacesTextAndSignatureThenSaves();
    void uiStaysLazyOnLongDocument();
    void uiScrollsWithWheel();
};

static void makePdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::Letter));
    writer.setResolution(72);
    QPainter painter(&writer);
    painter.drawText(72, 72, QStringLiteral("Hello"));
    painter.end();
}

void TestOmasign::saveWritesAnnotations() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdf = dir.filePath(QStringLiteral("doc.pdf"));
    makePdf(pdf);

    QImage signature(200, 80, QImage::Format_ARGB32);
    signature.fill(Qt::transparent);
    QPainter painter(&signature);
    painter.fillRect(20, 20, 160, 40, Qt::black);
    painter.end();
    const QString png = dir.filePath(QStringLiteral("sig.png"));
    QVERIFY(signature.save(png));

    Backend backend;
    QVERIFY(backend.open(QUrl::fromLocalFile(pdf)));
    QCOMPARE(backend.pageCount(), 1);
    QCOMPARE(backend.pageSize(0), QSizeF(612, 792));

    QVariantList items;
    items << QVariantMap{{"page", 0}, {"kind", "image"}, {"source", png},
                         {"fx", 0.6}, {"fy", 0.8}, {"fw", 0.3}, {"fh", 0.1}};
    items << QVariantMap{{"page", 0}, {"kind", "text"}, {"text", "Signed here"},
                         {"fx", 0.1}, {"fy", 0.1}, {"fw", 0.4}, {"fh", 0.04}, {"fs", 0.02}};
    QVERIFY(backend.save(items));
    QVERIFY(!QFile::exists(pdf + QStringLiteral(".omasign-tmp")));
    if (qEnvironmentVariableIsSet("OMASIGN_KEEP")) // keep the saved file for inspection with other renderers
        QFile::copy(pdf, qEnvironmentVariable("OMASIGN_KEEP"));

    const auto doc = Poppler::Document::load(pdf);
    QVERIFY(doc);
    const auto page = doc->page(0);
    const auto annotations = page->annotations();
    QCOMPARE(int(annotations.size()), 2);
    int stamps = 0, texts = 0;
    for (const auto &annotation : annotations) {
        if (annotation->subType() == Poppler::Annotation::AStamp) {
            ++stamps;
            const QRectF b = annotation->boundary();
            QVERIFY(qAbs(b.x() - 0.6) < 0.01 && qAbs(b.y() - 0.8) < 0.01);
            QVERIFY(qAbs(b.width() - 0.3) < 0.01 && qAbs(b.height() - 0.1) < 0.01);
        } else if (annotation->subType() == Poppler::Annotation::AText) {
            ++texts;
            QCOMPARE(annotation->contents(), QStringLiteral("Signed here"));
        }
    }
    QCOMPARE(stamps, 1);
    QCOMPARE(texts, 1);

    // The saved file must render: the stamp is dark, the text box has ink, paper stays white.
    const QImage rendered = backend.renderPage(0, 600);
    QVERIFY(!rendered.isNull());
    QVERIFY(qGray(rendered.pixel(int(0.75 * rendered.width()), int(0.85 * rendered.height()))) < 128);
    bool textInk = false;
    for (int y = int(0.1 * rendered.height()); y < int(0.14 * rendered.height()) && !textInk; ++y)
        for (int x = int(0.1 * rendered.width()); x < int(0.5 * rendered.width()); ++x)
            if (qGray(rendered.pixel(x, y)) < 128) { textInk = true; break; }
    QVERIFY(textInk);
    QVERIFY(qGray(rendered.pixel(rendered.width() / 2, rendered.height() / 2)) > 240);
}

void TestOmasign::addSignatureKeysOutPaper() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage scan(300, 100, QImage::Format_RGB32);
    scan.fill(Qt::white);
    QPainter painter(&scan);
    painter.fillRect(100, 40, 100, 20, Qt::black);
    painter.end();
    const QString path = dir.filePath(QStringLiteral("scan.png"));
    QVERIFY(scan.save(path));

    Backend backend;
    const int before = backend.signatures().size();
    QVERIFY(backend.addSignature(QUrl::fromLocalFile(path)));
    QCOMPARE(backend.signatures().size(), before + 1);

    const QString stored = backend.signatures().last();
    const QImage out(stored);
    QVERIFY(out.hasAlphaChannel());
    QCOMPARE(qAlpha(out.pixel(0, 0)), 0);                                   // paper is transparent
    QCOMPARE(qAlpha(out.pixel(out.width() / 2, out.height() / 2)), 255);    // ink is opaque
    QVERIFY(out.width() >= 100 && out.width() <= 110);                      // cropped to the ink plus margin
    QVERIFY(out.height() >= 20 && out.height() <= 30);

    backend.removeSignature(stored);
    QCOMPARE(backend.signatures().size(), before);
}

// Delegates have no QObject parent, so walk the visual tree instead of findChild().
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name)
        return root;
    const auto children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

static QQuickItem *findItem(QQuickWindow *window, const char *name) {
    QQuickItem *item = findItem(window->contentItem(), QString::fromLatin1(name));
    if (!item)
        qWarning("no item named %s", name);
    return item;
}

static QPoint centerOf(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

static void click(QQuickWindow *window, QQuickItem *item) {
    QTest::mouseClick(window, Qt::LeftButton, {}, centerOf(item));
}

static void drag(QQuickWindow *window, QPoint from, QPoint to) {
    QTest::mousePress(window, Qt::LeftButton, {}, from);
    for (int step = 1; step <= 8; ++step) {
        QTest::mouseMove(window, from + (to - from) * step / 8);
        QTest::qWait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, {}, to);
}

// OMASIGN_SHOTS=<dir> saves a screenshot of the window at each step, for eyeballing.
static void shot(QQuickWindow *window, const char *name) {
    const QString dir = qEnvironmentVariable("OMASIGN_SHOTS");
    if (dir.isEmpty())
        return;
    QTest::qWait(50);
    window->grabWindow().save(dir + QLatin1Char('/') + QString::fromLatin1(name) + QStringLiteral(".png"));
}

static QVariant row(QObject *model, int index, const char *role) {
    QJSValue value;
    QMetaObject::invokeMethod(model, "get", Q_RETURN_ARG(QJSValue, value), Q_ARG(int, index));
    return value.property(QString::fromLatin1(role)).toVariant();
}

void TestOmasign::uiPlacesTextAndSignatureThenSaves() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdf = dir.filePath(QStringLiteral("doc.pdf"));
    makePdf(pdf);

    Backend backend;
    const int signaturesBefore = backend.signatures().size();
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.addImageProvider(QStringLiteral("pdf"), new PageImageProvider(&backend));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window)); // shortcuts only fire in the active window
    QObject *model = window->property("itemModel").value<QObject *>();
    QVERIFY(model);

    QVERIFY(backend.open(QUrl::fromLocalFile(pdf)));
    QQuickItem *pages = findItem(window, "pages");
    QVERIFY(pages);
    QTRY_COMPARE(pages->property("count").toInt(), 1);
    shot(window, "1-opened");

    // Text: click the button, the new box is in edit mode with its placeholder selected.
    // Scroll down first: the new box must land in the visible part of the page.
    pages->setProperty("contentY", 400);
    click(window, findItem(window, "textButton"));
    QCOMPARE(model->property("count").toInt(), 1);
    QVERIFY(row(model, 0, "fy").toReal() > 0.3);
    QQuickItem *placed = findItem(window, "placed");
    QVERIFY(placed);
    QTRY_COMPARE(placed->property("editing").toBool(), true);
    for (const char c : {'J', 'a', 'n', 'e'})
        QTest::keyClick(window, c);
    QCOMPARE(row(model, 0, "text").toString(), QStringLiteral("Jane"));
    shot(window, "2-typing");
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(placed->property("editing").toBool(), false);

    // Click on bare paper deselects, click on the item selects, double-click edits again.
    QTest::mouseClick(window, Qt::LeftButton, {}, centerOf(placed) + QPoint(0, -150));
    QCOMPARE(window->property("selected").toInt(), -1);
    QTest::mouseClick(window, Qt::LeftButton, {}, centerOf(placed));
    QCOMPARE(window->property("selected").toInt(), 0);
    QCOMPARE(placed->property("editing").toBool(), false);
    QTest::mouseDClick(window, Qt::LeftButton, {}, centerOf(placed));
    QTRY_COMPARE(placed->property("editing").toBool(), true);
    QTest::keyClick(window, Qt::Key_End);
    QTest::keyClick(window, 'X');
    QCOMPARE(row(model, 0, "text").toString(), QStringLiteral("JaneX"));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(placed->property("editing").toBool(), false);

    // Drag moves it, the corner handle scales it.
    const qreal fx0 = row(model, 0, "fx").toReal();
    const qreal fs0 = row(model, 0, "fs").toReal();
    drag(window, centerOf(placed), centerOf(placed) + QPoint(120, 60));
    QTRY_VERIFY(row(model, 0, "fx").toReal() > fx0 + 0.05);
    QQuickItem *handle = findItem(window, "handle");
    QVERIFY(handle && handle->isVisible());
    drag(window, centerOf(handle), centerOf(handle) + QPoint(80, 0));
    QTRY_VERIFY(row(model, 0, "fs").toReal() > fs0 * 1.5);
    shot(window, "3-moved-resized");

    // Sign > Draw signature: a stroke on the pad, Done stores it and drops it on the page.
    click(window, findItem(window, "signButton"));
    QQuickItem *drawEntry = nullptr; // popup contents only exist once it is open
    QTRY_VERIFY((drawEntry = findItem(window->contentItem(), QStringLiteral("drawEntry"))) && drawEntry->isVisible());
    click(window, drawEntry);
    QQuickItem *pad = nullptr;
    QTRY_VERIFY((pad = findItem(window->contentItem(), QStringLiteral("pad"))) && pad->isVisible());
    QQuickItem *done = findItem(window, "padDone");
    QVERIFY(done);
    QCOMPARE(done->isEnabled(), false);
    const QPoint start = pad->mapToScene(QPointF(60, 100)).toPoint();
    drag(window, start, start + QPoint(300, 30));
    drag(window, start + QPoint(300, 30), start + QPoint(100, -60));
    QTRY_COMPARE(done->isEnabled(), true);
    shot(window, "4-pad");
    click(window, done);
    QTRY_COMPARE(backend.signatures().size(), signaturesBefore + 1);
    QTRY_COMPARE(model->property("count").toInt(), 2);
    QCOMPARE(row(model, 1, "kind").toString(), QStringLiteral("image"));
    QVERIFY(row(model, 1, "fw").toReal() > 0.2);
    // The box keeps the signature's shape: fh/fw equals the image aspect scaled by the page's.
    const QImage stored(backend.signatures().last());
    const qreal expectedFh = row(model, 1, "fw").toReal() * stored.height() / stored.width() / (792.0 / 612.0);
    QVERIFY2(qAbs(row(model, 1, "fh").toReal() - expectedFh) < 0.002,
             qPrintable(QStringLiteral("fh %1 expected %2").arg(row(model, 1, "fh").toReal()).arg(expectedFh)));

    // Delete removes the selected item; the menu card puts the stored signature back.
    QObject *padDialog = window->findChild<QObject *>(QStringLiteral("padDialog"));
    QVERIFY(padDialog);
    QTRY_COMPARE(padDialog->property("visible").toBool(), false); // modal popups eat keys until fully closed
    QTest::keyClick(window, Qt::Key_Delete);
    QCOMPARE(model->property("count").toInt(), 1);
    click(window, findItem(window, "signButton"));
    QObject *signMenu = window->findChild<QObject *>(QStringLiteral("signMenu"));
    QVERIFY(signMenu);
    QTRY_COMPARE(signMenu->property("opened").toBool(), true);
    QQuickItem *card = nullptr;
    QTRY_VERIFY((card = findItem(window->contentItem(), QStringLiteral("signatureCard"))) && card->isVisible());
    shot(window, "5-menu");
    click(window, card);
    QCOMPARE(model->property("count").toInt(), 2);
    shot(window, "6-placed");

    // Save writes both into the file and clears the overlay.
    click(window, findItem(window, "saveButton"));
    QTRY_COMPARE(model->property("count").toInt(), 0);
    QVERIFY2(backend.status().startsWith(QStringLiteral("Saved")), qPrintable(backend.status()));
    QTRY_VERIFY(backend.revision() >= 2);
    shot(window, "7-saved");
    const auto doc = Poppler::Document::load(pdf);
    QVERIFY(doc);
    QCOMPARE(int(doc->page(0)->annotations().size()), 2);

    backend.removeSignature(backend.signatures().last());
    QCOMPARE(backend.signatures().size(), signaturesBefore);
}

void TestOmasign::uiStaysLazyOnLongDocument() {
    const QString pdf = QStringLiteral("/usr/share/doc/ghostscript/Ghostscript.pdf"); // 499 pages
    if (!QFile::exists(pdf))
        QSKIP("no long sample PDF on this machine");

    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.addImageProvider(QStringLiteral("pdf"), new PageImageProvider(&backend));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(backend.open(QUrl::fromLocalFile(pdf)));
    QVERIFY2(timer.elapsed() < 2000, "opening must not render pages");
    QCOMPARE(backend.pageCount(), 499);

    QQuickItem *pages = findItem(window, "pages");
    QVERIFY(pages);
    QTRY_VERIFY(pages->property("count").toInt() == 499);
    QTest::qWait(200);
    // Only pages near the viewport get delegates; jumping to the end must not instantiate the rest.
    auto delegateCount = [pages] {
        int n = 0;
        const auto children = pages->property("contentItem").value<QQuickItem *>()->childItems();
        for (QQuickItem *child : children)
            if (child->property("pageH").isValid())
                ++n;
        return n;
    };
    QVERIFY2(delegateCount() < 12, qPrintable(QString::number(delegateCount())));
    QMetaObject::invokeMethod(pages, "scrollBy", Q_ARG(QVariant, 1e9));
    QTest::qWait(300);
    QVERIFY2(delegateCount() < 12, qPrintable(QString::number(delegateCount())));
    QVERIFY(pages->property("contentY").toReal() > 1000);
}

void TestOmasign::uiScrollsWithWheel() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pdf = dir.filePath(QStringLiteral("doc.pdf"));
    makePdf(pdf);

    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.addImageProvider(QStringLiteral("pdf"), new PageImageProvider(&backend));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    QVERIFY(backend.open(QUrl::fromLocalFile(pdf)));
    QQuickItem *pages = findItem(window, "pages");
    QVERIFY(pages);
    QTRY_COMPARE(pages->property("count").toInt(), 1);
    QTest::qWait(100);
    const qreal startY = pages->property("contentY").toReal();
    const QPointF pos(window->width() / 2, window->height() / 2);

    // Mouse wheel: one notch down.
    QWheelEvent mouseWheel(pos, window->mapToGlobal(pos.toPoint()), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &mouseWheel);
    QTRY_VERIFY2(pages->property("contentY").toReal() > startY + 10,
                 qPrintable(QString::number(pages->property("contentY").toReal())));

    // Touchpad: pixel deltas, device type TouchPad.
    const qreal midY = pages->property("contentY").toReal();
    QPointingDevice touchpad(QStringLiteral("test touchpad"), 2, QInputDevice::DeviceType::TouchPad,
                             QPointingDevice::PointerType::Finger, QInputDevice::Capability::Scroll, 1, 1);
    QWheelEvent padWheel(pos, window->mapToGlobal(pos.toPoint()), QPoint(0, -40), QPoint(0, -40),
                         Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false, Qt::MouseEventNotSynthesized, &touchpad);
    QCoreApplication::sendEvent(window, &padWheel);
    QTRY_VERIFY2(pages->property("contentY").toReal() > midY + 10,
                 qPrintable(QString::number(pages->property("contentY").toReal())));
}

QTEST_MAIN(TestOmasign)
#include "tst_omasign.moc"
