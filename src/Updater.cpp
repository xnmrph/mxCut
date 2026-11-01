#include "Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shellapi.h>
#endif

namespace {
const char *kReleaseApi = "https://api.github.com/repos/xnmrph/mxCut/releases/latest";
}

Updater::Updater(QObject *parent)
    : QObject(parent)
    , m_currentVersion(QCoreApplication::applicationVersion())
    , m_releaseApi(QString::fromLatin1(kReleaseApi))
{
}

Updater::~Updater()
{
    cancel();
}

bool Updater::supported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void Updater::setCurrentVersion(const QString &version)
{
    if (version == m_currentVersion)
        return;
    m_currentVersion = version;
    emit currentVersionChanged();
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    static const QRegularExpression notDigits(QStringLiteral("[^0-9]+"));
    const auto parts = [](QString v) {
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v = v.mid(1);
        // "1.2.3-beta" compares as 1.2.3.
        v = v.section(QLatin1Char('-'), 0, 0);
        QList<int> numbers;
        for (const QString &part : v.split(QLatin1Char('.')))
            numbers << QString(part).remove(notDigits).toInt();
        return numbers;
    };
    const QList<int> pa = parts(a), pb = parts(b);
    for (qsizetype i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = i < pa.size() ? pa.at(i) : 0;
        const int y = i < pb.size() ? pb.at(i) : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QNetworkAccessManager *Updater::network()
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    return m_network;
}

void Updater::setState(State state, const QString &error)
{
    if (state == m_state && error == m_errorString)
        return;
    m_state = state;
    m_errorString = error;
    emit stateChanged();
}

void Updater::setProgress(double progress)
{
    if (qFuzzyCompare(progress, m_progress))
        return;
    m_progress = progress;
    emit progressChanged();
}

void Updater::check()
{
    if (m_state == Checking || m_state == Downloading)
        return;
    setState(Checking);
    QNetworkRequest request(m_releaseApi);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("mxCut/%1").arg(m_currentVersion));
    request.setTransferTimeout(20000);
    m_reply = network()->get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            setState(Failed, tr("Couldn't reach GitHub to check for updates."));
            return;
        }
        if (!readRelease(reply->readAll())) {
            setState(Failed, tr("GitHub's answer about the latest release didn't make sense."));
            return;
        }
        const bool newer = compareVersions(m_latestVersion, m_currentVersion) > 0;
        // Only offer what this build can install itself.
        setState(newer && (!supported() || m_installerUrl.isValid()) ? Available : UpToDate);
    });
}

bool Updater::readRelease(const QByteArray &json)
{
    const QJsonObject release = QJsonDocument::fromJson(json).object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty() || release.value(QStringLiteral("draft")).toBool())
        return false;
    m_latestVersion = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    m_notes = release.value(QStringLiteral("body")).toString();
    m_releasePage = QUrl(release.value(QStringLiteral("html_url")).toString());
    m_installerUrl.clear();
    m_installerName.clear();
    m_installerSize = -1;
    m_installerSha256.clear();
    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QLatin1String("-setup.exe"), Qt::CaseInsensitive))
            continue;
        m_installerUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        m_installerName = name;
        m_installerSize = qint64(asset.value(QStringLiteral("size")).toDouble(-1));
        const QString digest = asset.value(QStringLiteral("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:")))
            m_installerSha256 = QByteArray::fromHex(digest.mid(7).toLatin1());
        break;
    }
    emit releaseChanged();
    return true;
}

void Updater::download()
{
    if (m_state == Downloading || !m_installerUrl.isValid())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_installerPath = QDir(dir).filePath(m_installerName.isEmpty() ? QStringLiteral("mxCut-setup.exe") : m_installerName);
    QFile::remove(m_installerPath);
    m_file = new QFile(m_installerPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        delete m_file;
        m_file = nullptr;
        setState(Failed, tr("Couldn't save the update in %1.").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    setProgress(0);
    setState(Downloading);

    QNetworkRequest request(m_installerUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("mxCut/%1").arg(m_currentVersion));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = network()->get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_file)
            m_file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total <= 0)
            total = m_installerSize;
        if (total > 0)
            setProgress(double(received) / double(total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != m_reply || !m_file)
            return;
        m_file->write(reply->readAll());
        m_file->close();
        const qint64 size = m_file->size();
        delete m_file;
        m_file = nullptr;
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            QFile::remove(m_installerPath);
            setState(Available);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_installerPath);
            setState(Failed, tr("The download didn't finish. Check your connection and try again."));
            return;
        }
        // Make sure it's whole, and the file GitHub says it is.
        bool ok = m_installerSize < 0 || size == m_installerSize;
        if (ok && !m_installerSha256.isEmpty()) {
            QFile file(m_installerPath);
            QCryptographicHash hash(QCryptographicHash::Sha256);
            ok = file.open(QIODevice::ReadOnly) && hash.addData(&file) && hash.result() == m_installerSha256;
        }
        if (!ok) {
            QFile::remove(m_installerPath);
            setState(Failed, tr("The downloaded update was damaged. Try again."));
            return;
        }
        setProgress(1);
        setState(ReadyToInstall);
    });
}

bool Updater::install()
{
    if (m_state != ReadyToInstall || !QFile::exists(m_installerPath))
        return false;
    // /UPDATE skips the installer's first pages and keeps the install folder.
    bool started;
#ifdef Q_OS_WIN
    // The installer asks for admin rights, which CreateProcess (and so
    // QProcess) can't give it; ShellExecute shows the UAC prompt instead.
    const std::wstring file = QDir::toNativeSeparators(m_installerPath).toStdWString();
    started = reinterpret_cast<qintptr>(ShellExecuteW(nullptr, L"open", file.c_str(), L"/UPDATE",
                                                      nullptr, SW_SHOWNORMAL)) > 32;
#else
    started = QProcess::startDetached(m_installerPath, {QStringLiteral("/UPDATE")});
#endif
    if (!started) {
        setState(Failed, tr("The installer couldn't be started. You can run it yourself from %1.")
                             .arg(QDir::toNativeSeparators(m_installerPath)));
        return false;
    }
    QCoreApplication::quit();
    return true;
}

void Updater::cancel()
{
    if (m_reply)
        m_reply->abort();
}
