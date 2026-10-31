#pragma once

#include "TrimJob.h"

#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// The user's choices, saved between runs.
class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool saveBesideSource READ saveBesideSource WRITE setSaveBesideSource NOTIFY changed)
    Q_PROPERTY(QUrl outputFolder READ outputFolder WRITE setOutputFolder NOTIFY changed)
    Q_PROPERTY(QString filenamePattern READ filenamePattern WRITE setFilenamePattern NOTIFY changed)
    Q_PROPERTY(bool overwrite READ overwrite WRITE setOverwrite NOTIFY changed)
    Q_PROPERTY(bool deleteOriginal READ deleteOriginal WRITE setDeleteOriginal NOTIFY changed)
    Q_PROPERTY(QString videoEncoder READ videoEncoder WRITE setVideoEncoder NOTIFY changed)
    Q_PROPERTY(int quality READ quality WRITE setQuality NOTIFY changed)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(bool copyAudio READ copyAudio WRITE setCopyAudio NOTIFY changed)
    Q_PROPERTY(int audioBitrate READ audioBitrate WRITE setAudioBitrate NOTIFY changed)
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY changed)
    Q_PROPERTY(QString ffmpegPath READ ffmpegPath WRITE setFfmpegPath NOTIFY ffmpegPathChanged)
    Q_PROPERTY(QString inKey READ inKey WRITE setInKey NOTIFY changed)
    Q_PROPERTY(QString outKey READ outKey WRITE setOutKey NOTIFY changed)
    // The folder open in the file browser, reopened at the next launch;
    // empty once the user closes it.
    Q_PROPERTY(QUrl lastFolder READ lastFolder WRITE setLastFolder NOTIFY changed)
    Q_PROPERTY(int folderSort READ folderSort WRITE setFolderSort NOTIFY changed)
    // Widths of the file browser and the export queue, as last dragged.
    Q_PROPERTY(int browserWidth READ browserWidth WRITE setBrowserWidth NOTIFY changed)
    Q_PROPERTY(int queueWidth READ queueWidth WRITE setQueueWidth NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);

    bool saveBesideSource() const;
    void setSaveBesideSource(bool value);
    QUrl outputFolder() const;
    void setOutputFolder(const QUrl &value);
    QString filenamePattern() const;
    void setFilenamePattern(const QString &value);
    bool overwrite() const;
    void setOverwrite(bool value);
    bool deleteOriginal() const;
    void setDeleteOriginal(bool value);
    QString videoEncoder() const;
    void setVideoEncoder(const QString &value);
    int quality() const;
    void setQuality(int value);
    int speed() const;
    void setSpeed(int value);
    bool copyAudio() const;
    void setCopyAudio(bool value);
    int audioBitrate() const;
    void setAudioBitrate(int value);
    int maxConcurrent() const;
    void setMaxConcurrent(int value);
    // Empty means find FFmpeg automatically.
    QString ffmpegPath() const;
    void setFfmpegPath(const QString &value);
    // Keys that set the in and out points, as QKeySequence text.
    QString inKey() const;
    void setInKey(const QString &value);
    QString outKey() const;
    void setOutKey(const QString &value);
    QUrl lastFolder() const;
    void setLastFolder(const QUrl &value);
    int folderSort() const;
    void setFolderSort(int value);
    int browserWidth() const;
    void setBrowserWidth(int value);
    int queueWidth() const;
    void setQueueWidth(int value);

    TrimOptions trimOptions() const;

    // The file name the current pattern gives, for the Settings preview.
    Q_INVOKABLE QString previewFileName(const QString &videoName, int mode) const;
    Q_INVOKABLE void resetEncoding();
    Q_INVOKABLE void resetShortcuts();

signals:
    void changed();
    void ffmpegPathChanged();

private:
    QVariant value(const char *key, const QVariant &fallback) const;
    void store(const char *key, const QVariant &value);

    QSettings m_store;
};
