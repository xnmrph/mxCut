#include "ExportQueue.h"
#include "testclip.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestExportQueue : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void runsEveryJobIntoItsOwnFile();
    void requeueUpdatesWaitingJob();
    void finishedVideoGetsNewEntry();
    void respectsConcurrencyLimit();
    void failedJobDoesNotStopTheRest();
    void cancelStopsTheQueue();
    void removeAndClearFinished();
    void usesSettingsWhenJobsStart();
    void queuesSegments();
    void namesEachOutput();

private:
    QUrl clip(int n) const { return QUrl::fromLocalFile(m_clips.at(n)); }
    static bool waitForAll(ExportQueue &queue);

    QTemporaryDir m_dir;
    QStringList m_clips;
};

void TestExportQueue::initTestCase()
{
    if (TrimJob::ffmpegPath().isEmpty())
        QSKIP("ffmpeg is required");
    QVERIFY(m_dir.isValid());
    for (int i = 0; i < 3; ++i) {
        m_clips << m_dir.filePath(QStringLiteral("clip%1.mp4").arg(i));
        QVERIFY(makeTestClip(m_clips.last(), 8));
    }
}

bool TestExportQueue::waitForAll(ExportQueue &queue)
{
    QSignalSpy done(&queue, &ExportQueue::allFinished);
    return !queue.running() || done.wait(60000);
}

void TestExportQueue::runsEveryJobIntoItsOwnFile()
{
    ExportQueue queue;
    for (int i = 0; i < 3; ++i)
        queue.add(clip(i), 1.0, 3.0, TrimJob::Fast);
    QCOMPARE(queue.count(), 3);
    QCOMPARE(queue.pendingCount(), 3);

    queue.start();
    QVERIFY(queue.running());
    QVERIFY(waitForAll(queue));

    QCOMPARE(queue.finishedCount(), 3);
    QSet<QString> outputs;
    for (int i = 0; i < 3; ++i) {
        const QString out = queue.data(queue.index(i), ExportQueue::OutputRole).toString();
        QVERIFY2(QFileInfo::exists(out), qPrintable(out));
        outputs.insert(out);
        QFile::remove(out);
    }
    QCOMPARE(outputs.size(), 3);
}

void TestExportQueue::requeueUpdatesWaitingJob()
{
    ExportQueue queue;
    QSignalSpy inserted(&queue, &QAbstractItemModel::rowsInserted);
    queue.add(clip(0), 1.0, 2.0, TrimJob::Fast);
    queue.add(clip(0), 3.0, 5.0, TrimJob::Accurate);

    QCOMPARE(queue.count(), 1);
    QCOMPARE(inserted.count(), 1);
    TrimJob *job = queue.jobFor(clip(0));
    QVERIFY(job);
    QCOMPARE(job->inPoint(), 3.0);
    QCOMPARE(job->outPoint(), 5.0);
    QCOMPARE(job->mode(), TrimJob::Accurate);
}

void TestExportQueue::finishedVideoGetsNewEntry()
{
    ExportQueue queue;
    queue.add(clip(1), 1.0, 2.0, TrimJob::Fast);
    queue.start();
    QVERIFY(waitForAll(queue));
    QCOMPARE(queue.finishedCount(), 1);

    // A second cut of the same video must not overwrite the finished one.
    queue.add(clip(1), 4.0, 6.0, TrimJob::Fast);
    QCOMPARE(queue.count(), 2);
    queue.start();
    QVERIFY(waitForAll(queue));
    const QString first = queue.data(queue.index(0), ExportQueue::OutputRole).toString();
    const QString second = queue.data(queue.index(1), ExportQueue::OutputRole).toString();
    QVERIFY(first != second);
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));
    QFile::remove(first);
    QFile::remove(second);
}

void TestExportQueue::respectsConcurrencyLimit()
{
    ExportQueue queue;
    queue.setMaxConcurrent(1);
    for (int i = 0; i < 3; ++i)
        queue.add(clip(i), 0.0, 8.0, TrimJob::Accurate);

    int peak = 0;
    auto track = [&] {
        int running = 0;
        for (int i = 0; i < queue.count(); ++i)
            running += queue.data(queue.index(i), ExportQueue::StateRole).toInt() == TrimJob::Running;
        peak = qMax(peak, running);
    };
    connect(&queue, &ExportQueue::statsChanged, this, track);

    queue.start();
    track();
    QVERIFY(waitForAll(queue));
    QCOMPARE(queue.finishedCount(), 3);
    QCOMPARE(peak, 1);
    for (int i = 0; i < 3; ++i)
        QFile::remove(queue.data(queue.index(i), ExportQueue::OutputRole).toString());
}

void TestExportQueue::failedJobDoesNotStopTheRest()
{
    ExportQueue queue;
    queue.add(QUrl::fromLocalFile(m_dir.filePath("missing.mp4")), 0.0, 1.0, TrimJob::Fast);
    queue.add(clip(2), 1.0, 2.0, TrimJob::Fast);
    queue.start();
    QVERIFY(waitForAll(queue));
    QCOMPARE(queue.failedCount(), 1);
    QCOMPARE(queue.finishedCount(), 1);
    QFile::remove(queue.data(queue.index(1), ExportQueue::OutputRole).toString());
}

void TestExportQueue::cancelStopsTheQueue()
{
    ExportQueue queue;
    queue.setMaxConcurrent(1);
    queue.add(clip(0), 0.0, 8.0, TrimJob::Accurate);
    queue.add(clip(1), 0.0, 8.0, TrimJob::Accurate);
    queue.start();
    queue.cancel();

    QVERIFY(!queue.running());
    QCOMPARE(queue.jobFor(clip(0))->state(), TrimJob::Cancelled);
    QCOMPARE(queue.jobFor(clip(1))->state(), TrimJob::Idle);
    QTest::qWait(200);
    QCOMPARE(queue.jobFor(clip(1))->state(), TrimJob::Idle);
}

void TestExportQueue::removeAndClearFinished()
{
    ExportQueue queue;
    queue.add(clip(0), 1.0, 2.0, TrimJob::Fast);
    queue.add(clip(1), 1.0, 2.0, TrimJob::Fast);
    queue.remove(0);
    QCOMPARE(queue.count(), 1);
    QVERIFY(!queue.jobFor(clip(0)));

    queue.start();
    QVERIFY(waitForAll(queue));
    const QString out = queue.data(queue.index(0), ExportQueue::OutputRole).toString();
    queue.clearFinished();
    QCOMPARE(queue.count(), 0);
    QVERIFY(QFileInfo::exists(out));
    QFile::remove(out);
}

void TestExportQueue::usesSettingsWhenJobsStart()
{
    // Without an organisation name, Windows has no registry key to keep
    // settings in, so nothing set here would be read back.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mxcut-tests");
    QCoreApplication::setApplicationName("tst_exportqueue");
    QSettings().clear();
    Settings settings;
    ExportQueue queue;
    queue.setSettings(&settings);
    queue.add(clip(0), 1.0, 2.0, TrimJob::Fast);

    // Changed after queuing but before starting: the new choice wins.
    settings.setOutputFolder(QUrl::fromLocalFile(m_dir.filePath("exports")));
    settings.setSaveBesideSource(false);
    settings.setFilenamePattern("{name}-cut");
    queue.start();
    QVERIFY(waitForAll(queue));

    const QString out = queue.data(queue.index(0), ExportQueue::OutputRole).toString();
    QCOMPARE(out, QDir(m_dir.filePath("exports")).filePath("clip0-cut.mp4"));
    QVERIFY(QFileInfo::exists(out));
    QSettings().clear();
}

void TestExportQueue::queuesSegments()
{
    ExportQueue queue;
    const QVariantList parts{
        QVariantMap{{"inPoint", 1.0}, {"outPoint", 2.0}},
        QVariantMap{{"inPoint", 5.0}, {"outPoint", 6.5}},
    };
    queue.add(clip(2), 1.0, 2.0, TrimJob::Accurate, parts);
    const QModelIndex row = queue.index(0);
    QCOMPARE(queue.data(row, ExportQueue::SegmentCountRole).toInt(), 2);
    QCOMPARE(queue.data(row, ExportQueue::KeptDurationRole).toDouble(), 2.5);
    QCOMPARE(queue.jobFor(clip(2))->segments().size(), 2);

    // Queuing it again with one segment goes back to a plain cut.
    queue.add(clip(2), 3.0, 4.0, TrimJob::Accurate);
    QCOMPARE(queue.count(), 1);
    QCOMPARE(queue.data(row, ExportQueue::SegmentCountRole).toInt(), 1);
    QCOMPARE(queue.data(row, ExportQueue::InPointRole).toDouble(), 3.0);

    queue.add(clip(2), 1.0, 2.0, TrimJob::Accurate, parts);
    queue.start();
    QVERIFY(waitForAll(queue));
    QCOMPARE(queue.finishedCount(), 1);
    QFile::remove(queue.data(row, ExportQueue::OutputRole).toString());
}

void TestExportQueue::namesEachOutput()
{
    ExportQueue queue;
    queue.add(clip(0), 1.0, 2.0, TrimJob::Fast);
    queue.add(clip(1), 1.0, 2.0, TrimJob::Fast);
    const QModelIndex first = queue.index(0);
    const QModelIndex second = queue.index(1);
    QCOMPARE(queue.data(first, ExportQueue::DefaultNameRole).toString(), QStringLiteral("clip0_trim"));
    QCOMPARE(queue.data(first, ExportQueue::ExtensionRole).toString(), QStringLiteral("mp4"));

    QSignalSpy changed(&queue, &QAbstractItemModel::dataChanged);
    // A typed extension is not doubled.
    queue.setOutputName(0, QStringLiteral("beach day.mp4"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(queue.data(first, ExportQueue::OutputNameRole).toString(), QStringLiteral("beach day.mp4"));

    // Queuing the video again keeps its name.
    queue.add(clip(0), 1.5, 2.5, TrimJob::Fast);
    QCOMPARE(queue.data(first, ExportQueue::OutputNameRole).toString(), QStringLiteral("beach day.mp4"));

    queue.start();
    QVERIFY(waitForAll(queue));
    QCOMPARE(queue.finishedCount(), 2);
    const QString named = queue.data(first, ExportQueue::OutputRole).toString();
    const QString patterned = queue.data(second, ExportQueue::OutputRole).toString();
    QCOMPARE(QFileInfo(named).fileName(), QStringLiteral("beach day.mp4"));
    QCOMPARE(QFileInfo(patterned).fileName(), QStringLiteral("clip1_trim.mp4"));
    QVERIFY(QFileInfo::exists(named));
    QFile::remove(named);
    QFile::remove(patterned);
}

QTEST_GUILESS_MAIN(TestExportQueue)
#include "tst_exportqueue.moc"
