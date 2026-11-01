#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

// Looks for a newer mxCut on GitHub and, on Windows, downloads its installer
// and runs it. Elsewhere it only reports what it finds.
class Updater : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Whether this build can update itself (the Windows installer build).
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QString currentVersion READ currentVersion WRITE setCurrentVersion NOTIFY currentVersionChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY releaseChanged)
    // The release's notes, as Markdown.
    Q_PROPERTY(QString notes READ notes NOTIFY releaseChanged)
    Q_PROPERTY(QUrl releasePage READ releasePage NOTIFY releaseChanged)
    // 0..1 while downloading.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)

public:
    enum State { Idle, Checking, UpToDate, Available, Downloading, ReadyToInstall, Failed };
    Q_ENUM(State)

    explicit Updater(QObject *parent = nullptr);
    ~Updater() override;

    static bool supported();
    QString currentVersion() const { return m_currentVersion; }
    void setCurrentVersion(const QString &version);
    State state() const { return m_state; }
    QString latestVersion() const { return m_latestVersion; }
    QString notes() const { return m_notes; }
    QUrl releasePage() const { return m_releasePage; }
    double progress() const { return m_progress; }
    QString errorString() const { return m_errorString; }

    // Asks GitHub for the latest release.
    Q_INVOKABLE void check();
    // Downloads the new installer to a temporary folder.
    Q_INVOKABLE void download();
    // Starts the downloaded installer for an update, then quits mxCut so
    // it can replace the program. False if it couldn't be started.
    Q_INVOKABLE bool install();
    Q_INVOKABLE void cancel();

    // Where releases are looked up; tests point it elsewhere.
    void setReleaseApi(const QUrl &url) { m_releaseApi = url; }
    // Reads a GitHub "latest release" reply; false if it isn't one.
    bool readRelease(const QByteArray &json);
    QUrl installerUrl() const { return m_installerUrl; }

    // Negative, zero or positive as a is older, the same as or newer than b.
    // "v1.2.10" is newer than "1.2.9"; missing parts count as 0.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void currentVersionChanged();
    void stateChanged();
    void releaseChanged();
    void progressChanged();

private:
    void setState(State state, const QString &error = {});
    void setProgress(double progress);
    QNetworkAccessManager *network();

    QString m_currentVersion;
    State m_state = Idle;
    QString m_latestVersion;
    QString m_notes;
    QUrl m_releasePage;
    QUrl m_installerUrl;
    QString m_installerName;
    qint64 m_installerSize = -1;
    QByteArray m_installerSha256;
    QString m_installerPath;
    double m_progress = 0;
    QString m_errorString;
    QUrl m_releaseApi;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile *m_file = nullptr;
};
