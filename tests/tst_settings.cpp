#include "Settings.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultsMatchTrimOptions();
    void persistsAcrossInstances();
    void outputFolderOnlyWhenChosen();
    void rejectsUnknownEncoder();
    void previewFileName();
    void deleteOriginalKeepsOldChoice();
    void ffmpegPathOverride();
    void shortcutKeys();
    void fileBrowser();
};

void TestSettings::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mxcut-tests");
    QCoreApplication::setApplicationName("tst_settings");
    QSettings().clear();
}

void TestSettings::defaultsMatchTrimOptions()
{
    Settings settings;
    const TrimOptions o = settings.trimOptions();
    const TrimOptions d;
    QCOMPARE(o.outputFolder, QString());
    QCOMPARE(o.filenamePattern, d.filenamePattern);
    QCOMPARE(o.videoEncoder, d.videoEncoder);
    QCOMPARE(o.quality, d.quality);
    QCOMPARE(o.copyAudio, d.copyAudio);
    QCOMPARE(settings.maxConcurrent(), 2);
}

void TestSettings::persistsAcrossInstances()
{
    {
        Settings settings;
        QSignalSpy changed(&settings, &Settings::changed);
        settings.setVideoEncoder("libx265");
        settings.setQuality(24);
        settings.setSpeed(2);
        settings.setCopyAudio(true);
        settings.setMaxConcurrent(4);
        settings.setFilenamePattern("{name}_{date}");
        QCOMPARE(changed.count(), 6);
        settings.setQuality(24);
        QCOMPARE(changed.count(), 6);
    }
    Settings again;
    QCOMPARE(again.videoEncoder(), QStringLiteral("libx265"));
    QCOMPARE(again.quality(), 24);
    QCOMPARE(again.speed(), 2);
    QVERIFY(again.copyAudio());
    QCOMPARE(again.maxConcurrent(), 4);
    QCOMPARE(again.filenamePattern(), QStringLiteral("{name}_{date}"));

    again.resetEncoding();
    QCOMPARE(again.videoEncoder(), QStringLiteral("libx264"));
    QCOMPARE(again.quality(), 18);
    QVERIFY(!again.copyAudio());
    again.setFilenamePattern("{name}_trim");
    again.setMaxConcurrent(2);
}

void TestSettings::outputFolderOnlyWhenChosen()
{
    Settings settings;
    settings.setOutputFolder(QUrl::fromLocalFile("/tmp/out"));
    QCOMPARE(settings.trimOptions().outputFolder, QString());
    settings.setSaveBesideSource(false);
    QCOMPARE(settings.trimOptions().outputFolder, QStringLiteral("/tmp/out"));
    settings.setSaveBesideSource(true);
}

void TestSettings::rejectsUnknownEncoder()
{
    Settings settings;
    settings.setVideoEncoder("not_an_encoder");
    QCOMPARE(settings.videoEncoder(), QStringLiteral("libx264"));
}

void TestSettings::previewFileName()
{
    Settings settings;
    settings.setFilenamePattern("{name} [{in}]");
    QCOMPARE(settings.previewFileName("holiday.mov", 0), QStringLiteral("holiday [00-01-23.500].mov"));
    QCOMPARE(settings.previewFileName("holiday.mov", 1), QStringLiteral("holiday [00-01-23.500].mp4"));
    settings.setFilenamePattern("{name}_trim");
}

void TestSettings::deleteOriginalKeepsOldChoice()
{
    QSettings().setValue("output/replaceOriginal", true);
    {
        Settings settings;
        QVERIFY(settings.deleteOriginal());
        QVERIFY(settings.trimOptions().deleteOriginal);
        settings.setDeleteOriginal(false);
        QVERIFY(!settings.deleteOriginal());
    }
    QVERIFY(!Settings().deleteOriginal());
    QSettings().remove("output/replaceOriginal");
    QSettings().remove("output/deleteOriginal");
}

void TestSettings::ffmpegPathOverride()
{
    Settings settings;
    const QString found = TrimJob::ffmpegPath();
    settings.setFfmpegPath("/definitely/not/ffmpeg");
    QCOMPARE(TrimJob::ffmpegPath(), QString());
    settings.setFfmpegPath({});
    QCOMPARE(TrimJob::ffmpegPath(), found);
}

void TestSettings::shortcutKeys()
{
    {
        Settings settings;
        QCOMPARE(settings.inKey(), QStringLiteral("A"));
        QCOMPARE(settings.outKey(), QStringLiteral("D"));
        settings.setInKey("J");
        settings.setOutKey("L");
    }
    Settings again;
    QCOMPARE(again.inKey(), QStringLiteral("J"));
    QCOMPARE(again.outKey(), QStringLiteral("L"));
    again.setOutKey("");
    QCOMPARE(again.outKey(), QStringLiteral("D"));
    again.resetShortcuts();
    QCOMPARE(again.inKey(), QStringLiteral("A"));
}

void TestSettings::fileBrowser()
{
    {
        Settings settings;
        QCOMPARE(settings.lastFolder(), QUrl());
        QCOMPARE(settings.browserWidth(), 380);
        QCOMPARE(settings.queueWidth(), 300);
        settings.setLastFolder(QUrl::fromLocalFile("/tmp/clips"));
        settings.setFolderSort(2);
        settings.setBrowserWidth(250);
        settings.setQueueWidth(-5);
    }
    Settings again;
    QCOMPARE(again.lastFolder(), QUrl::fromLocalFile("/tmp/clips"));
    QCOMPARE(again.folderSort(), 2);
    QCOMPARE(again.browserWidth(), 250);
    QCOMPARE(again.queueWidth(), 0);
    // Closing the folder forgets it.
    again.setLastFolder(QUrl());
    QCOMPARE(Settings().lastFolder(), QUrl());
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
