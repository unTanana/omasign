#pragma once

#include <QFileSystemWatcher>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSizeF>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <memory>

namespace Poppler {
class Document;
}

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY fileChanged)
    Q_PROPERTY(int revision READ revision NOTIFY fileChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList signatures READ signatures NOTIFY signaturesChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    QUrl fileUrl() const { return m_fileUrl; }
    QString fileName() const;
    int pageCount() const;
    int revision() const { return m_revision; }
    QString status() const { return m_status; }
    QStringList signatures() const { return m_signatures; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }

    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE QSizeF pageSize(int page) const; // in PDF points
    // items: list of maps {page, kind: "image"|"text", source, text, fx, fy, fw, fh, fs}
    // with fx/fy/fw/fh as fractions of the page and fs as font size over page width.
    Q_INVOKABLE bool save(const QVariantList &items);
    Q_INVOKABLE bool saveAs(const QUrl &url, const QVariantList &items);
    Q_INVOKABLE bool addSignature(const QUrl &source);
    Q_INVOKABLE bool addSignatureImage(const QVariant &image); // a QImage, e.g. from grabToImage
    Q_INVOKABLE void removeSignature(const QString &path);
    Q_INVOKABLE qreal imageAspect(const QString &path) const;

    QImage renderPage(int page, int widthPx);

signals:
    void fileChanged();
    void statusChanged();
    void signaturesChanged();
    void themeChanged();
    void textScaleChanged();

private:
    void setStatus(const QString &status);
    bool writeTo(const QString &target, const QVariantList &items);
    QString signaturesDir() const;
    bool storeSignature(QImage image);
    void refreshSignatures();
    void loadOmarchyTheme();
    void watchOmarchyTheme();

    // m_mutex guards m_doc, which the image provider renders from on its own thread.
    // Page sizes are copied out at open so the UI thread never waits on a render.
    QMutex m_mutex;
    std::unique_ptr<Poppler::Document> m_doc;
    QList<QSizeF> m_pageSizes;
    QUrl m_fileUrl;
    int m_revision = 0;
    QString m_status;
    QStringList m_signatures;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QFileSystemWatcher m_themeWatcher;
};

class PageImageProvider : public QQuickImageProvider {
public:
    explicit PageImageProvider(Backend *backend);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Backend *m_backend;
};
