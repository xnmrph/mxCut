#include "ExportQueue.h"
#include "VideoFolderModel.h"
#include "testclip.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestVideoFolderModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void listsFoldersThenVideosInNaturalOrder();
    void reportsQueueState();
    void sortsAndGroupsByDay();
    void filtersAndSearches();
    void stepsThroughVideosShown();
    void parentFolder();
    void makesThumbnailsAndDurations();
    void seesNewFiles();

private:
    static void touch(const QString &path, const QDateTime &modified = {})
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        if (modified.isValid())
            QVERIFY(f.setFileTime(modified, QFileDevice::FileModificationTime));
    }
    static QStringList names(const VideoFolderModel &model)
    {
        QStringList list;
        for (int i = 0; i < model.count(); ++i)
            list << model.data(model.index(i), VideoFolderModel::NameRole).toString();
        return list;
    }
};

void TestVideoFolderModel::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
}

void TestVideoFolderModel::listsFoldersThenVideosInNaturalOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (const char *name : {"clip 10.mp4", "clip 2.MKV", "Clip 1.mov", "notes.txt", "cover.jpg"})
        touch(dir.filePath(name));
    QVERIFY(QDir(dir.path()).mkdir("sub.mp4"));
    touch(dir.filePath("sub.mp4/inside.mp4"));

    VideoFolderModel model;
    model.setFolder(QUrl::fromLocalFile(dir.path()));

    QCOMPARE(model.count(), 4);
    QCOMPARE(model.videoCount(), 3);
    QCOMPARE(names(model), QStringList({"sub.mp4", "Clip 1.mov", "clip 2.MKV", "clip 10.mp4"}));
    QVERIFY(model.data(model.index(0), VideoFolderModel::IsFolderRole).toBool());
    QCOMPARE(model.data(model.index(0), VideoFolderModel::ItemCountRole).toInt(), 1);
    QVERIFY(!model.data(model.index(1), VideoFolderModel::IsFolderRole).toBool());
    QCOMPARE(model.indexOf(model.urlAt(2)), 2);
    QCOMPARE(model.urlAt(5), QUrl());
    QCOMPARE(model.firstVideo(), QUrl::fromLocalFile(dir.filePath("Clip 1.mov")));
    QCOMPARE(model.get(3).value("name").toString(), QStringLiteral("clip 10.mp4"));
}

void TestVideoFolderModel::reportsQueueState()
{
    QTemporaryDir dir;
    touch(dir.filePath("a.mp4"));
    touch(dir.filePath("b.mp4"));

    ExportQueue queue;
    VideoFolderModel model;
    model.setQueue(&queue);
    model.setFolder(QUrl::fromLocalFile(dir.path()));
    QCOMPARE(model.data(model.index(0), VideoFolderModel::QueueStateRole).toInt(), -1);
    QCOMPARE(model.queuedCount(), 0);

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    queue.add(model.urlAt(1), 0.0, 1.0, TrimJob::Fast);
    QVERIFY(!changed.isEmpty());
    QCOMPARE(changed.last().at(0).toModelIndex().row(), 1);
    QCOMPARE(model.data(model.index(1), VideoFolderModel::QueueStateRole).toInt(), int(TrimJob::Idle));
    QCOMPARE(model.data(model.index(0), VideoFolderModel::QueueStateRole).toInt(), -1);
    QCOMPARE(model.queuedCount(), 1);

    queue.remove(0);
    QCOMPARE(model.data(model.index(1), VideoFolderModel::QueueStateRole).toInt(), -1);
    QCOMPARE(model.queuedCount(), 0);
}

void TestVideoFolderModel::sortsAndGroupsByDay()
{
    QTemporaryDir dir;
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime today(QDate::currentDate(), QTime(0, 0, 1));
    touch(dir.filePath("old.mp4"), now.addDays(-30));
    touch(dir.filePath("big.mp4"), today);
    touch(dir.filePath("new.mp4"), today.addSecs(1));
    touch(dir.filePath("yesterday.mp4"), today.addDays(-1));
    {
        QFile f(dir.filePath("big.mp4"));
        QVERIFY(f.open(QIODevice::Append));
        f.write(QByteArray(1000, 'x'));
        f.close();
        QVERIFY(f.open(QIODevice::Append));
        QVERIFY(f.setFileTime(today, QFileDevice::FileModificationTime));
    }
    QVERIFY(QDir(dir.path()).mkdir("folder"));

    VideoFolderModel model;
    model.setFolder(QUrl::fromLocalFile(dir.path()));
    model.setSort(VideoFolderModel::Newest);
    QCOMPARE(names(model), QStringList({"folder", "new.mp4", "big.mp4", "yesterday.mp4", "old.mp4"}));
    const QVariantList groups = model.groups();
    QCOMPARE(groups.size(), 4);
    QCOMPARE(groups.at(0).toMap().value("title").toString(), QStringLiteral("Folders"));
    QCOMPARE(groups.at(1).toMap().value("title").toString(), QStringLiteral("Today"));
    QCOMPARE(groups.at(1).toMap().value("start").toInt(), 1);
    QCOMPARE(groups.at(1).toMap().value("count").toInt(), 2);
    QCOMPARE(groups.at(2).toMap().value("title").toString(), QStringLiteral("Yesterday"));
    QCOMPARE(groups.at(3).toMap().value("start").toInt(), 4);

    model.setSort(VideoFolderModel::Oldest);
    QCOMPARE(names(model), QStringList({"folder", "old.mp4", "yesterday.mp4", "big.mp4", "new.mp4"}));

    model.setSort(VideoFolderModel::Largest);
    QCOMPARE(names(model).at(1), QStringLiteral("big.mp4"));
    QCOMPARE(model.groups().size(), 2);
    QCOMPARE(model.groups().at(1).toMap().value("title").toString(), QStringLiteral("Videos"));

    model.setSort(VideoFolderModel::Name);
    QCOMPARE(names(model), QStringList({"folder", "big.mp4", "new.mp4", "old.mp4", "yesterday.mp4"}));
}

void TestVideoFolderModel::filtersAndSearches()
{
    QTemporaryDir dir;
    touch(dir.filePath("Beach.mp4"));
    touch(dir.filePath("drone.mp4"));
    touch(dir.filePath("beach 2.mp4"));
    QVERIFY(QDir(dir.path()).mkdir("beach folder"));

    ExportQueue queue;
    VideoFolderModel model;
    model.setQueue(&queue);
    model.setFolder(QUrl::fromLocalFile(dir.path()));

    model.setSearch("BEACH");
    QCOMPARE(names(model), QStringList({"beach folder", "beach 2.mp4", "Beach.mp4"}));
    model.setSearch({});
    QCOMPARE(model.count(), 4);

    model.setFilter(VideoFolderModel::Queued);
    QCOMPARE(model.count(), 0);
    queue.add(QUrl::fromLocalFile(dir.filePath("drone.mp4")), 0.0, 1.0, TrimJob::Fast);
    QCOMPARE(names(model), QStringList({"drone.mp4"}));
    QCOMPARE(model.videoCount(), 3);

    model.setFilter(VideoFolderModel::Trimmed);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.trimmedCount(), 0);
}

void TestVideoFolderModel::stepsThroughVideosShown()
{
    QTemporaryDir dir;
    for (const char *name : {"a.mp4", "b.mp4", "c.mp4"})
        touch(dir.filePath(name));
    QVERIFY(QDir(dir.path()).mkdir("folder"));

    VideoFolderModel model;
    model.setFolder(QUrl::fromLocalFile(dir.path()));
    const auto url = [&dir](const char *name) { return QUrl::fromLocalFile(dir.filePath(name)); };
    QCOMPARE(model.adjacentVideo(url("a.mp4"), 1), url("b.mp4"));
    QCOMPARE(model.adjacentVideo(url("b.mp4"), -1), url("a.mp4"));
    // Folders are skipped, and there is nothing before the first video.
    QCOMPARE(model.adjacentVideo(url("a.mp4"), -1), QUrl());
    QCOMPARE(model.adjacentVideo(url("c.mp4"), 1), QUrl());

    model.setSearch("c");
    QCOMPARE(model.adjacentVideo(url("a.mp4"), 1), url("c.mp4"));
}

void TestVideoFolderModel::parentFolder()
{
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkdir("inner"));
    VideoFolderModel model;
    model.setFolder(QUrl::fromLocalFile(dir.filePath("inner")));
    QCOMPARE(model.folderName(), QStringLiteral("inner"));
    QCOMPARE(QDir(model.parentFolder().toLocalFile()), QDir(dir.path()));
    model.setFolder(QUrl::fromLocalFile(QDir::rootPath()));
    QCOMPARE(model.parentFolder(), QUrl());
}

void TestVideoFolderModel::makesThumbnailsAndDurations()
{
    if (TrimJob::ffmpegPath().isEmpty())
        QSKIP("FFmpeg is not installed");
    QTemporaryDir dir;
    QVERIFY(makeTestClip(dir.filePath("long.mp4"), 4));
    // Shorter than the second in that thumbnails are taken from.
    QProcess p;
    p.start(TrimJob::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                    "testsrc2=size=320x240:rate=30:duration=0.5", dir.filePath("short.mp4")});
    QVERIFY(p.waitForFinished(60000) && p.exitCode() == 0);

    VideoFolderModel model;
    model.setSort(VideoFolderModel::Name);
    model.setFolder(QUrl::fromLocalFile(dir.path()));
    QCOMPARE(model.count(), 2);
    const auto ready = [&model](int row) {
        return !model.data(model.index(row), VideoFolderModel::ThumbnailRole).toString().isEmpty();
    };
    QTRY_VERIFY_WITH_TIMEOUT(ready(0) && ready(1), 30000);
    QVERIFY(qAbs(model.data(model.index(0), VideoFolderModel::DurationRole).toDouble() - 4.0) < 0.2);
    QVERIFY(qAbs(model.data(model.index(1), VideoFolderModel::DurationRole).toDouble() - 0.5) < 0.2);
    const QString thumb = QUrl(model.data(model.index(0), VideoFolderModel::ThumbnailRole).toString()).toLocalFile();
    QVERIFY(QFileInfo(thumb).size() > 0);

    // Cached: a second look has them straight away.
    VideoFolderModel again;
    again.setSort(VideoFolderModel::Name);
    again.setFolder(QUrl::fromLocalFile(dir.path()));
    QVERIFY(!again.data(again.index(0), VideoFolderModel::ThumbnailRole).toString().isEmpty());
    QVERIFY(again.data(again.index(1), VideoFolderModel::DurationRole).toDouble() > 0);

    // Queued cuts are fractions of the video.
    ExportQueue queue;
    again.setQueue(&queue);
    queue.add(again.urlAt(0), 1.0, 3.0, TrimJob::Fast);
    const QVariantList cuts = again.data(again.index(0), VideoFolderModel::CutsRole).toList();
    QCOMPARE(cuts.size(), 1);
    QVERIFY(qAbs(cuts.at(0).toMap().value("start").toDouble() - 0.25) < 0.02);
    QVERIFY(qAbs(cuts.at(0).toMap().value("end").toDouble() - 0.75) < 0.02);
}

void TestVideoFolderModel::seesNewFiles()
{
    QTemporaryDir dir;
    touch(dir.filePath("a.mp4"));
    VideoFolderModel model;
    model.setFolder(QUrl::fromLocalFile(dir.path()));
    QCOMPARE(model.videoCount(), 1);
    touch(dir.filePath("b.mp4"));
    QTRY_COMPARE_WITH_TIMEOUT(model.videoCount(), 2, 5000);
}

QTEST_GUILESS_MAIN(TestVideoFolderModel)
#include "tst_videofoldermodel.moc"
