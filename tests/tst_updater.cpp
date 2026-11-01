#include "Updater.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestUpdater : public QObject
{
    Q_OBJECT

private slots:
    void comparesVersions();
    void readsRelease();
    void ignoresDrafts();
    void checksAndDownloads();
    void rejectsDamagedDownload();
    void upToDate();

private:
    static QByteArray release(const QString &tag, const QUrl &installer = {}, qint64 size = 0,
                              const QByteArray &sha = {})
    {
        QJsonArray assets;
        assets.append(QJsonObject{{"name", "mxCut-x86_64.AppImage"},
                                  {"browser_download_url", "https://example.com/app.AppImage"},
                                  {"size", 1}});
        if (installer.isValid()) {
            QJsonObject asset{{"name", "mxCut-" + tag.mid(1) + "-setup.exe"},
                              {"browser_download_url", installer.toString()},
                              {"size", double(size)}};
            if (!sha.isEmpty())
                asset["digest"] = "sha256:" + QString::fromLatin1(sha.toHex());
            assets.append(asset);
        }
        return QJsonDocument(QJsonObject{{"tag_name", tag},
                                         {"body", "## What's Changed\n* Things"},
                                         {"html_url", "https://github.com/xnmrph/mxCut/releases/tag/" + tag},
                                         {"draft", false},
                                         {"assets", assets}})
            .toJson();
    }
    static QUrl writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &data)
    {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(data);
        return QUrl::fromLocalFile(f.fileName());
    }
};

void TestUpdater::comparesVersions()
{
    QVERIFY(Updater::compareVersions("1.0.7", "1.0.6") > 0);
    QVERIFY(Updater::compareVersions("v1.0.10", "1.0.9") > 0);
    QVERIFY(Updater::compareVersions("1.0", "1.0.0") == 0);
    QVERIFY(Updater::compareVersions("1.1.0", "1.0.99") > 0);
    QVERIFY(Updater::compareVersions("1.0.6", "1.0.7") < 0);
    QVERIFY(Updater::compareVersions("2.0.0-beta", "2.0.0") == 0);
}

void TestUpdater::readsRelease()
{
    Updater updater;
    QVERIFY(updater.readRelease(release("v1.2.3", QUrl("https://example.com/setup.exe"), 42)));
    QCOMPARE(updater.latestVersion(), QStringLiteral("1.2.3"));
    QCOMPARE(updater.installerUrl(), QUrl("https://example.com/setup.exe"));
    QVERIFY(updater.notes().contains("Things"));
    QCOMPARE(updater.releasePage(), QUrl("https://github.com/xnmrph/mxCut/releases/tag/v1.2.3"));
    QVERIFY(!updater.readRelease("not json"));
}

void TestUpdater::ignoresDrafts()
{
    Updater updater;
    QJsonObject draft = QJsonDocument::fromJson(release("v9.0.0")).object();
    draft["draft"] = true;
    QVERIFY(!updater.readRelease(QJsonDocument(draft).toJson()));
}

void TestUpdater::checksAndDownloads()
{
    QTemporaryDir dir;
    const QByteArray installer(100000, 'x');
    const QUrl installerUrl = writeFile(dir, "setup.exe", installer);
    const QByteArray sha = QCryptographicHash::hash(installer, QCryptographicHash::Sha256);
    const QUrl api = writeFile(dir, "latest.json", release("v99.0.0", installerUrl, installer.size(), sha));

    Updater updater;
    updater.setCurrentVersion("1.0.7");
    updater.setReleaseApi(api);
    updater.check();
    QTRY_VERIFY(updater.state() != Updater::Checking);
    if (Updater::supported())
        QCOMPARE(updater.state(), Updater::Available);
    QCOMPARE(updater.latestVersion(), QStringLiteral("99.0.0"));

    updater.download();
    QTRY_VERIFY(updater.state() != Updater::Downloading);
    QCOMPARE(updater.state(), Updater::ReadyToInstall);
    QCOMPARE(updater.progress(), 1.0);
}

void TestUpdater::rejectsDamagedDownload()
{
    QTemporaryDir dir;
    const QUrl installerUrl = writeFile(dir, "setup.exe", QByteArray(1000, 'x'));
    const QUrl api = writeFile(dir, "latest.json",
                               release("v99.0.0", installerUrl, 1000, QByteArray(32, '\0')));
    Updater updater;
    updater.setReleaseApi(api);
    updater.check();
    QTRY_VERIFY(updater.state() != Updater::Checking);
    updater.download();
    QTRY_VERIFY(updater.state() != Updater::Downloading);
    QCOMPARE(updater.state(), Updater::Failed);
    QVERIFY(!updater.errorString().isEmpty());
}

void TestUpdater::upToDate()
{
    QTemporaryDir dir;
    const QUrl api = writeFile(dir, "latest.json", release("v1.0.7", QUrl("https://example.com/s.exe"), 1));
    Updater updater;
    updater.setCurrentVersion("1.0.7");
    updater.setReleaseApi(api);
    updater.check();
    QTRY_VERIFY(updater.state() != Updater::Checking);
    QCOMPARE(updater.state(), Updater::UpToDate);

    updater.setReleaseApi(QUrl::fromLocalFile(dir.filePath("missing.json")));
    updater.check();
    QTRY_VERIFY(updater.state() != Updater::Checking);
    QCOMPARE(updater.state(), Updater::Failed);
}

QTEST_GUILESS_MAIN(TestUpdater)
#include "tst_updater.moc"
