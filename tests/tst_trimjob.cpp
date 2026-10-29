#include "TrimJob.h"
#include "testclip.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestTrimJob : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void outputPathFor();
    void friendlyErrors();
    void fastTrimSnapsToKeyframe();
    void accurateTrimIsFrameExact();
    void rejectsEmptyRange();
    void cancelRemovesPartialOutput();
    void encoderOptions();
    void customOutputFolder();
    void reportsUnreadableInput();
    void deletesOriginalInOtherFolder();
    void deletedOriginalGivesItsName();
    void deleteKeepsOtherFiles();
    void failedTrimKeepsOriginal();
    void joinsSegments_data();
    void joinsSegments();

private:
    double probeDuration(const QString &path);
    bool runTrim(TrimJob &job);

    QTemporaryDir m_dir;
    QString m_clip;
};

void TestTrimJob::initTestCase()
{
    if (TrimJob::ffmpegPath().isEmpty() || QStandardPaths::findExecutable("ffprobe").isEmpty())
        QSKIP("ffmpeg and ffprobe are required");
    QVERIFY(m_dir.isValid());

    // 20 s at 30 fps with a keyframe every 2 s, so fast and accurate cuts differ.
    m_clip = m_dir.filePath("clip.mp4");
    QVERIFY(makeTestClip(m_clip));
}

double TestTrimJob::probeDuration(const QString &path)
{
    QProcess p;
    p.start(QStandardPaths::findExecutable("ffprobe"), {
        "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=duration", "-of", "csv=p=0", path,
    });
    p.waitForFinished(10000);
    return p.readAllStandardOutput().trimmed().toDouble();
}

bool TestTrimJob::runTrim(TrimJob &job)
{
    QSignalSpy done(&job, &TrimJob::finished);
    job.start();
    return done.count() > 0 || done.wait(60000);
}

void TestTrimJob::outputPathFor()
{
    TrimOptions options;
    const QString fast = TrimJob::outputPathFor(m_clip, TrimJob::Fast, options);
    QCOMPARE(QFileInfo(fast).fileName(), QStringLiteral("clip_trim.mp4"));

    QFile existing(fast);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.close();
    QCOMPARE(QFileInfo(TrimJob::outputPathFor(m_clip, TrimJob::Fast, options)).fileName(),
             QStringLiteral("clip_trim_2.mp4"));
    options.overwrite = true;
    QCOMPARE(TrimJob::outputPathFor(m_clip, TrimJob::Fast, options), fast);
    QFile::remove(fast);

    // Never the source itself, even when overwriting.
    options.filenamePattern = QStringLiteral("{name}");
    QCOMPARE(QFileInfo(TrimJob::outputPathFor(m_clip, TrimJob::Fast, options)).fileName(),
             QStringLiteral("clip_2.mp4"));
    // Unless the source is being deleted: then the trim can take its name.
    options.deleteOriginal = true;
    QCOMPARE(TrimJob::outputPathFor(m_clip, TrimJob::Fast, options), m_clip);

    options = {};
    QCOMPARE(QFileInfo(TrimJob::outputPathFor("/x/a.webm", TrimJob::Accurate, options)).suffix(), QStringLiteral("mp4"));
    QCOMPARE(QFileInfo(TrimJob::outputPathFor("/x/a.mkv", TrimJob::Accurate, options)).suffix(), QStringLiteral("mkv"));
    QCOMPARE(QFileInfo(TrimJob::outputPathFor("/x/a.webm", TrimJob::Fast, options)).suffix(), QStringLiteral("webm"));

    options.outputFolder = m_dir.filePath("out");
    options.filenamePattern = QStringLiteral("{name} {in}-{out} {mode}: <cut>?");
    QCOMPARE(TrimJob::outputPathFor("/x/a.mov", TrimJob::Accurate, options, 83.5, 3725.25),
             QDir(m_dir.filePath("out")).filePath("a 00-01-23.500-01-02-05.250 accurate_ _cut__.mp4"));
}

void TestTrimJob::friendlyErrors()
{
    QVERIFY(TrimJob::friendlyError("[h264_nvenc @ 0x1] Cannot load libnvidia-encode.so.1", "h264_nvenc").contains("GPU"));
    QVERIFY(TrimJob::friendlyError("Unknown encoder 'libfoo'", "libfoo").contains("libfoo"));
    QVERIFY(TrimJob::friendlyError("x.mp4: Invalid data found when processing input").contains("can't read"));
    QVERIFY(TrimJob::friendlyError("av_interleaved_write_frame(): No space left on device").contains("disk is full"));
    QCOMPARE(TrimJob::friendlyError("line one\nsomething odd happened\n"), QStringLiteral("something odd happened"));
}

void TestTrimJob::fastTrimSnapsToKeyframe()
{
    TrimJob job;
    job.setSource(QUrl::fromLocalFile(m_clip));
    job.setInPoint(5.0);
    job.setOutPoint(9.0);
    job.setMode(TrimJob::Fast);
    job.setOutput(m_dir.filePath("fast.mp4"));
    QVERIFY(runTrim(job));
    QCOMPARE(job.state(), TrimJob::Finished);
    QCOMPARE(job.progress(), 1.0);

    // Starts at the keyframe at 4 s, so up to 2 s longer than requested.
    const double d = probeDuration(job.output());
    QVERIFY2(d >= 3.9 && d <= 6.1, qPrintable(QString::number(d)));
}

void TestTrimJob::accurateTrimIsFrameExact()
{
    TrimJob job;
    job.setSource(QUrl::fromLocalFile(m_clip));
    job.setInPoint(5.0);
    job.setOutPoint(9.0);
    job.setMode(TrimJob::Accurate);
    job.setOutput(m_dir.filePath("accurate.mp4"));
    QVERIFY(runTrim(job));
    QCOMPARE(job.state(), TrimJob::Finished);

    const double d = probeDuration(job.output());
    QVERIFY2(qAbs(d - 4.0) < 0.05, qPrintable(QString::number(d)));
}

void TestTrimJob::rejectsEmptyRange()
{
    TrimJob job;
    job.setSource(QUrl::fromLocalFile(m_clip));
    job.setInPoint(5.0);
    job.setOutPoint(5.0);
    QVERIFY(runTrim(job));
    QCOMPARE(job.state(), TrimJob::Failed);
    QVERIFY(!job.errorString().isEmpty());
}

void TestTrimJob::cancelRemovesPartialOutput()
{
    TrimJob job;
    job.setSource(QUrl::fromLocalFile(m_clip));
    job.setInPoint(0);
    job.setOutPoint(20);
    job.setMode(TrimJob::Accurate);
    job.setOutput(m_dir.filePath("cancelled.mp4"));
    job.start();
    QCOMPARE(job.state(), TrimJob::Running);
    job.cancel();
    QCOMPARE(job.state(), TrimJob::Cancelled);
    QVERIFY(!QFileInfo::exists(job.output()));
}

void TestTrimJob::encoderOptions()
{
    TrimJob job;
    TrimOptions options;
    options.videoEncoder = QStringLiteral("libx265");
    options.quality = 28;
    options.copyAudio = true;
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(m_clip));
    job.setInPoint(2.0);
    job.setOutPoint(4.0);
    job.setMode(TrimJob::Accurate);
    job.setOutput(m_dir.filePath("hevc.mp4"));

    const QStringList args = job.arguments();
    QVERIFY(args.contains("libx265"));
    QCOMPARE(args.at(args.indexOf("-crf") + 1), QStringLiteral("28"));
    QCOMPARE(args.at(args.indexOf("-c:a") + 1), QStringLiteral("copy"));
    QVERIFY(args.contains("hvc1"));

    if (!QProcess::execute(TrimJob::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                                   "color=s=64x64:d=0.1", "-c:v", "libx265", "-f", "null", "-"})) {
        QVERIFY(runTrim(job));
        QCOMPARE(job.state(), TrimJob::Finished);
        QProcess p;
        p.start(QStandardPaths::findExecutable("ffprobe"), {"-v", "error", "-select_streams", "v:0",
                                                            "-show_entries", "stream=codec_name", "-of", "csv=p=0", job.output()});
        p.waitForFinished(10000);
        QCOMPARE(p.readAllStandardOutput().trimmed(), QByteArray("hevc"));
    }
}

void TestTrimJob::customOutputFolder()
{
    TrimJob job;
    TrimOptions options;
    options.outputFolder = m_dir.filePath("nested/out");
    options.filenamePattern = QStringLiteral("{name}-{mode}");
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(m_clip));
    job.setInPoint(1.0);
    job.setOutPoint(2.0);
    QVERIFY(runTrim(job));
    QCOMPARE(job.state(), TrimJob::Finished);
    QCOMPARE(job.output(), QDir(m_dir.filePath("nested/out")).filePath("clip-fast.mp4"));
    QVERIFY(QFileInfo::exists(job.output()));
}

void TestTrimJob::reportsUnreadableInput()
{
    const QString junk = m_dir.filePath("junk.mp4");
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not a video");
    f.close();

    TrimJob job;
    job.setSource(QUrl::fromLocalFile(junk));
    job.setInPoint(0.0);
    job.setOutPoint(1.0);
    QVERIFY(runTrim(job));
    QCOMPARE(job.state(), TrimJob::Failed);
    QVERIFY2(job.errorString().contains("can't read"), qPrintable(job.errorString()));
    QVERIFY(!job.errorDetails().isEmpty());
}

void TestTrimJob::deletesOriginalInOtherFolder()
{
    const QString video = m_dir.filePath("elsewhere.mp4");
    QVERIFY(QFile::copy(m_clip, video));

    TrimJob job;
    TrimOptions options;
    options.deleteOriginal = true;
    options.outputFolder = m_dir.filePath("other");
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(video));
    job.setInPoint(2.0);
    job.setOutPoint(6.0);
    QSignalSpy about(&job, &TrimJob::aboutToDeleteOriginal);
    QSignalSpy deleted(&job, &TrimJob::deleteOriginalFinished);
    QVERIFY(runTrim(job));
    QVERIFY2(job.state() == TrimJob::Finished, qPrintable(job.errorString()));
    QCOMPARE(about.count(), 1);
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(deleted.first().at(1).toUrl(), QUrl::fromLocalFile(job.output()));
    QVERIFY(deleted.first().at(2).toBool());
    QCOMPARE(job.output(), QDir(m_dir.filePath("other")).filePath("elsewhere_trim.mp4"));
    QVERIFY(!QFileInfo::exists(video));

    const double d = probeDuration(job.output());
    QVERIFY2(qAbs(d - 4.0) < 0.1, qPrintable(QString::number(d)));
}

void TestTrimJob::deletedOriginalGivesItsName()
{
    const QString video = m_dir.filePath("replace.mp4");
    QVERIFY(QFile::copy(m_clip, video));

    TrimJob job;
    TrimOptions options;
    options.deleteOriginal = true;
    options.filenamePattern = "{name}";
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(video));
    job.setInPoint(2.0);
    job.setOutPoint(6.0);
    QSignalSpy deleted(&job, &TrimJob::deleteOriginalFinished);
    QVERIFY(runTrim(job));
    QVERIFY2(job.state() == TrimJob::Finished, qPrintable(job.errorString()));
    QCOMPARE(deleted.count(), 1);
    QCOMPARE(deleted.first().at(1).toUrl(), QUrl::fromLocalFile(video));
    QCOMPARE(job.output(), video);

    const double d = probeDuration(video);
    QVERIFY2(qAbs(d - 4.0) < 0.1, qPrintable(QString::number(d)));
    QVERIFY(QDir(m_dir.path()).entryList({"*mxcut-part*"}).isEmpty());
}

void TestTrimJob::deleteKeepsOtherFiles()
{
    // Accurate mode turns keep.mov into keep.mp4, which is already taken.
    const QString video = m_dir.filePath("keep.mov");
    const QString other = m_dir.filePath("keep.mp4");
    QVERIFY(QFile::copy(m_clip, video));
    QFile f(other);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("someone else's file");
    f.close();

    TrimJob job;
    TrimOptions options;
    options.deleteOriginal = true;
    options.filenamePattern = "{name}";
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(video));
    job.setInPoint(1.0);
    job.setOutPoint(2.0);
    job.setMode(TrimJob::Accurate);
    QVERIFY(runTrim(job));
    QVERIFY2(job.state() == TrimJob::Finished, qPrintable(job.errorString()));
    QVERIFY(!QFileInfo::exists(video));
    QCOMPARE(QFileInfo(other).size(), qint64(19));
    QCOMPARE(QFileInfo(job.output()).fileName(), QStringLiteral("keep_2.mp4"));
    QVERIFY(QFileInfo::exists(job.output()));
}

void TestTrimJob::failedTrimKeepsOriginal()
{
    const QString junk = m_dir.filePath("junk-kept.mp4");
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not a video");
    f.close();

    TrimJob job;
    TrimOptions options;
    options.deleteOriginal = true;
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(junk));
    job.setInPoint(0.0);
    job.setOutPoint(1.0);
    QSignalSpy about(&job, &TrimJob::aboutToDeleteOriginal);
    QVERIFY(runTrim(job));
    QCOMPARE(job.state(), TrimJob::Failed);
    QCOMPARE(about.count(), 0);
    QVERIFY(QFileInfo::exists(junk));
}

void TestTrimJob::joinsSegments_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("replace");
    QTest::addColumn<bool>("silent");
    QTest::newRow("fast") << int(TrimJob::Fast) << false << false;
    QTest::newRow("accurate") << int(TrimJob::Accurate) << false << false;
    QTest::newRow("accurate, deleting original") << int(TrimJob::Accurate) << true << false;
    QTest::newRow("accurate, no audio") << int(TrimJob::Accurate) << false << true;
}

void TestTrimJob::joinsSegments()
{
    QFETCH(int, mode);
    QFETCH(bool, replace);
    QFETCH(bool, silent);
    QTemporaryDir dir;
    const QString video = dir.filePath("parts.mp4");
    if (silent) {
        QProcess p;
        p.start(TrimJob::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                                        "-i", "testsrc2=size=160x120:rate=30:duration=20", "-c:v", "libx264",
                                        "-g", "60", video});
        QVERIFY(p.waitForFinished(60000) && p.exitCode() == 0);
    } else {
        QVERIFY(QFile::copy(m_clip, video));
    }

    TrimJob job;
    TrimOptions options;
    options.deleteOriginal = replace;
    if (replace)
        options.filenamePattern = "{name}";
    job.setOptions(options);
    job.setSource(QUrl::fromLocalFile(video));
    job.setMode(TrimJob::Mode(mode));
    // Given out of order; they are joined in time order.
    job.setSegments({
        QVariantMap{{"inPoint", 12.0}, {"outPoint", 14.0}},
        QVariantMap{{"inPoint", 4.0}, {"outPoint", 7.0}},
    });
    QCOMPARE(job.inPoint(), 4.0);
    QCOMPARE(job.outPoint(), 14.0);
    QCOMPARE(job.keptDuration(), 5.0);
    QVERIFY(runTrim(job));
    QVERIFY2(job.state() == TrimJob::Finished, qPrintable(job.errorString() + "\n" + job.errorDetails()));
    QCOMPARE(job.progress(), 1.0);

    if (replace)
        QCOMPARE(job.output(), video);
    const double d = probeDuration(job.output());
    // Fast cuts snap to keyframes (every 2 s here), the joined length with them.
    if (mode == TrimJob::Accurate)
        QVERIFY2(qAbs(d - 5.0) < 0.1, qPrintable(QString::number(d)));
    else
        QVERIFY2(d >= 4.9 && d < 9.1, qPrintable(QString::number(d)));
    QStringList left = QDir(dir.path()).entryList(QDir::Files);
    left.removeAll("parts.mp4");
    left.removeAll(QFileInfo(job.output()).fileName());
    QVERIFY2(left.isEmpty(), qPrintable(left.join(", ")));
}

QTEST_GUILESS_MAIN(TestTrimJob)
#include "tst_trimjob.moc"
