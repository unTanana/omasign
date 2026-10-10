#include "backend.h"

#include <poppler-annotation.h>
#include <poppler-qt6.h>

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImageReader>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

Backend::Backend(QObject *parent) : QObject(parent) {
    loadOmarchyTheme();
    watchOmarchyTheme();
    const auto reloadTheme = [this] {
        loadOmarchyTheme();
        watchOmarchyTheme();
    };
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, reloadTheme);
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, reloadTheme);
    refreshSignatures();
}

Backend::~Backend() = default;

QString Backend::fileName() const {
    return m_fileUrl.isEmpty() ? QStringLiteral("Omasign")
                               : QFileInfo(m_fileUrl.toLocalFile()).fileName();
}

int Backend::pageCount() const {
    return m_pageSizes.size();
}

void Backend::setStatus(const QString &status) {
    m_status = status;
    emit statusChanged();
}

bool Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return false;
    }
    const QString path = url.toLocalFile();
    auto doc = Poppler::Document::load(path);
    if (!doc || doc->isLocked()) {
        setStatus(QStringLiteral("Could not open %1.").arg(QFileInfo(path).fileName()));
        return false;
    }
    doc->setRenderHint(Poppler::Document::Antialiasing, true);
    doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
    QList<QSizeF> sizes;
    for (int i = 0; i < doc->numPages(); ++i) {
        const auto page = doc->page(i);
        sizes << (page ? page->pageSizeF() : QSizeF(612, 792));
    }
    {
        QMutexLocker lock(&m_mutex);
        m_doc = std::move(doc);
        m_pageSizes = sizes;
    }
    m_fileUrl = url;
    ++m_revision;
    emit fileChanged();
    return true;
}

QSizeF Backend::pageSize(int page) const {
    return m_pageSizes.value(page, QSizeF(612, 792));
}

QImage Backend::renderPage(int page, int widthPx) {
    QMutexLocker lock(&m_mutex);
    if (!m_doc || page < 0 || page >= m_doc->numPages())
        return {};
    const auto p = m_doc->page(page);
    if (!p || p->pageSizeF().width() <= 0)
        return {};
    const qreal dpi = 72.0 * widthPx / p->pageSizeF().width();
    return p->renderToImage(dpi, dpi);
}

bool Backend::save(const QVariantList &items) {
    if (!m_fileUrl.isLocalFile()) {
        setStatus(QStringLiteral("Nothing to save."));
        return false;
    }
    return writeTo(m_fileUrl.toLocalFile(), items);
}

bool Backend::saveAs(const QUrl &url, const QVariantList &items) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be saved."));
        return false;
    }
    return writeTo(url.toLocalFile(), items);
}

bool Backend::writeTo(const QString &target, const QVariantList &items) {
    if (!m_doc) {
        setStatus(QStringLiteral("Nothing to save."));
        return false;
    }
    const QString tmp = target + QStringLiteral(".omasign-tmp");
    bool ok = false;
    {
        QMutexLocker lock(&m_mutex);
        QSet<int> touched;
        for (const QVariant &v : items) {
            const QVariantMap item = v.toMap();
            const int pageIndex = item.value(QStringLiteral("page")).toInt();
            const auto page = m_doc->page(pageIndex);
            if (!page)
                continue;
            touched.insert(pageIndex);
            const QRectF rect(item.value(QStringLiteral("fx")).toReal(),
                              item.value(QStringLiteral("fy")).toReal(),
                              item.value(QStringLiteral("fw")).toReal(),
                              item.value(QStringLiteral("fh")).toReal());
            if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("image")) {
                const QImage image(item.value(QStringLiteral("source")).toString());
                if (image.isNull())
                    continue;
                Poppler::StampAnnotation stamp;
                stamp.setBoundary(rect);
                stamp.setStampCustomImage(image);
                page->addAnnotation(&stamp);
            } else {
                const QString text = item.value(QStringLiteral("text")).toString().trimmed();
                if (text.isEmpty())
                    continue;
                Poppler::TextAnnotation note(Poppler::TextAnnotation::InPlace);
                note.setBoundary(rect);
                note.setContents(text);
                QFont font(QStringLiteral("Helvetica"));
                font.setPointSizeF(item.value(QStringLiteral("fs")).toReal() * page->pageSizeF().width());
                note.setTextFont(font);
                note.setTextColor(Qt::black);
                Poppler::Annotation::Style style = note.style();
                style.setWidth(0);
                note.setStyle(style);
                page->addAnnotation(&note);
            }
        }
        // Poppler only builds a text annotation's appearance stream when it draws
        // the page. Without one, other viewers have to guess the font, so render
        // each touched page once before writing.
        for (const int i : touched) {
            const auto page = m_doc->page(i);
            if (!page)
                continue;
            page->renderToImage(2, 2);
            // The generated appearance lives only in memory until it is written back
            // onto the annotation.
            for (const auto &annotation : page->annotations({Poppler::Annotation::AText})) {
                if (const auto appearance = annotation->annotationAppearance())
                    annotation->setAnnotationAppearance(*appearance);
            }
        }
        auto converter = m_doc->pdfConverter();
        converter->setOutputFileName(tmp);
        converter->setPDFOptions(Poppler::PDFConverter::WithChanges);
        ok = converter->convert();
    }
    // Rename over the original only once the whole file is on disk, so a crash
    // mid-save never leaves a half-written document behind.
    if (ok)
        ok = std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(target).constData()) == 0;
    if (!ok)
        QFile::remove(tmp);
    // Reload either way: success shows the saved annotations as part of the
    // page, failure drops the half-applied ones from memory.
    open(ok ? QUrl::fromLocalFile(target) : m_fileUrl);
    setStatus(ok ? QStringLiteral("Saved %1").arg(fileName())
                 : QStringLiteral("Could not save %1.").arg(QFileInfo(target).fileName()));
    return ok;
}

QString Backend::signaturesDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/signatures");
}

void Backend::refreshSignatures() {
    QStringList found;
    const QDir dir(signaturesDir());
    const QStringList names = dir.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QString &name : names)
        found << dir.absoluteFilePath(name);
    m_signatures = found;
    emit signaturesChanged();
}

bool Backend::addSignature(const QUrl &source) {
    const QImage image(source.toLocalFile());
    if (image.isNull()) {
        setStatus(QStringLiteral("Could not read that image."));
        return false;
    }
    return storeSignature(image);
}

bool Backend::addSignatureImage(const QVariant &image) {
    return storeSignature(image.value<QImage>());
}

bool Backend::storeSignature(QImage image) {
    if (image.isNull()) {
        setStatus(QStringLiteral("Nothing drawn."));
        return false;
    }
    image = image.convertToFormat(QImage::Format_ARGB32);

    bool transparent = false;
    for (int y = 0; y < image.height() && !transparent; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) < 255) {
                transparent = true;
                break;
            }
        }
    }
    // A photo or scan has no alpha: treat the paper as transparent and keep
    // the ink colour, the darker the pixel the more opaque it becomes.
    if (!transparent) {
        for (int y = 0; y < image.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb px = line[x];
                int ink = 255 - qMin(qRed(px), qMin(qGreen(px), qBlue(px)));
                if (ink < 40)
                    ink = 0;
                line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), ink);
            }
        }
    }

    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 20) {
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
    }
    if (right < 0) {
        setStatus(QStringLiteral("Nothing drawn."));
        return false;
    }
    QRect box(QPoint(left, top), QPoint(right, bottom));
    const int margin = qMax(2, box.width() / 40);
    box.adjust(-margin, -margin, margin, margin);
    image = image.copy(box & image.rect());

    QDir().mkpath(signaturesDir());
    const QString dest = signaturesDir() + QLatin1Char('/')
        + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))
        + QStringLiteral(".png");
    if (!image.save(dest)) {
        setStatus(QStringLiteral("Could not store the signature."));
        return false;
    }
    refreshSignatures();
    return true;
}

void Backend::removeSignature(const QString &path) {
    if (QFileInfo(path).absolutePath() == QDir(signaturesDir()).absolutePath())
        QFile::remove(path);
    refreshSignatures();
}

qreal Backend::imageAspect(const QString &path) const {
    const QSize size = QImageReader(path).size();
    return size.width() > 0 ? qreal(size.height()) / size.width() : 0.4;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;
    m_darkMode = darkMode;
    loadOmarchyTheme();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;
    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;
            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
        }
    }

    if (themeMode == QStringLiteral("dark")) {
        m_darkMode = true;
    } else if (themeMode == QStringLiteral("light")) {
        m_darkMode = false;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            m_darkMode = luminance < 0.5;
        }
    }
    emit themeChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");
    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

PageImageProvider::PageImageProvider(Backend *backend)
    : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading),
      m_backend(backend) {}

QImage PageImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    // id is "<page>/<revision>"; the revision only busts Qt's image cache after a save.
    const int page = id.section(QLatin1Char('/'), 0, 0).toInt();
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 800;
    const QImage image = m_backend->renderPage(page, width);
    if (size)
        *size = image.size();
    return image;
}
