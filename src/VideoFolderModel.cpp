#include "VideoFolderModel.h"

#include "ExportQueue.h"
#include "MediaPreview.h"
#include "TrimJob.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

namespace {

// "clip 2" sorts before "clip 10", ignoring case. QCollator's numeric mode
// does this too, but not in every locale (it is a no-op in the C locale).
bool naturalLess(const QString &a, const QString &b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype ei = i, ej = j;
            while (ei < a.size() && a[ei].isDigit())
                ++ei;
            while (ej < b.size() && b[ej].isDigit())
                ++ej;
            // Compare digit runs by value: strip leading zeros, then length, then digits.
            QStringView da = QStringView(a).mid(i, ei - i), db = QStringView(b).mid(j, ej - j);
            while (da.size() > 1 && da.front() == u'0')
                da = da.mid(1);
            while (db.size() > 1 && db.front() == u'0')
                db = db.mid(1);
            if (da.size() != db.size())
                return da.size() < db.size();
            if (const int c = da.compare(db); c != 0)
                return c < 0;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a[i].toCaseFolded(), cb = b[j].toCaseFolded();
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    if ((a.size() - i) != (b.size() - j))
        return (a.size() - i) < (b.size() - j);
    return a < b;
}

constexpr int kMaxThumbnailJobs = 2;
constexpr int kThumbnailWidth = 320;

QStringList videoFilters()
{
    QStringList filters;
    for (const QString &ext : VideoFolderModel::videoExtensions())
        filters << QStringLiteral("*.") + ext;
    return filters;
}

// "Today", "Yesterday", "Oct 24", or "Oct 24, 2025" for other years.
QString dayTitle(const QDate &day)
{
    const QDate today = QDate::currentDate();
    if (day == today)
        return QStringLiteral("Today");
    if (day == today.addDays(-1))
        return QStringLiteral("Yesterday");
    return QLocale().toString(day, day.year() == today.year() ? QStringLiteral("MMM d") : QStringLiteral("MMM d, yyyy"));
}

} // namespace

VideoFolderModel::VideoFolderModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(400);
    connect(&m_refreshTimer, &QTimer::timeout, this, &VideoFolderModel::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_refreshTimer, qOverload<>(&QTimer::start));
}

VideoFolderModel::~VideoFolderModel()
{
    stopThumbnails();
}

const QStringList &VideoFolderModel::videoExtensions()
{
    static const QStringList extensions{
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"), QStringLiteral("mov"),
        QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("ts"), QStringLiteral("mts"),
        QStringLiteral("m2ts"), QStringLiteral("flv"), QStringLiteral("wmv"), QStringLiteral("mpg"),
        QStringLiteral("mpeg"), QStringLiteral("3gp"), QStringLiteral("ogv"),
    };
    return extensions;
}

int VideoFolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int VideoFolderModel::queueState(const Entry &entry) const
{
    if (entry.isDir || !m_queue)
        return -1;
    const TrimJob *job = m_queue->jobFor(QUrl::fromLocalFile(entry.path));
    return job ? int(job->state()) : -1;
}

QVariant VideoFolderModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Entry &entry = m_entries.at(m_rows.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case UrlRole:
        return QUrl::fromLocalFile(entry.path);
    case SizeRole:
        return entry.size;
    case QueueStateRole:
        return queueState(entry);
    case IsFolderRole:
        return entry.isDir;
    case ModifiedRole:
        return entry.modified;
    case ThumbnailRole:
        return entry.thumbnail;
    case DurationRole:
        return entry.duration;
    case ItemCountRole:
        return entry.itemCount;
    case CutsRole: {
        QVariantList cuts;
        const TrimJob *job = (m_queue && !entry.isDir) ? m_queue->jobFor(QUrl::fromLocalFile(entry.path)) : nullptr;
        if (!job || entry.duration <= 0)
            return cuts;
        QVariantList parts = job->segments();
        if (parts.isEmpty())
            parts << QVariantMap{{QStringLiteral("inPoint"), job->inPoint()}, {QStringLiteral("outPoint"), job->outPoint()}};
        for (const QVariant &part : std::as_const(parts)) {
            const QVariantMap m = part.toMap();
            cuts << QVariantMap{
                {QStringLiteral("start"), qBound(0.0, m.value(QStringLiteral("inPoint")).toDouble() / entry.duration, 1.0)},
                {QStringLiteral("end"), qBound(0.0, m.value(QStringLiteral("outPoint")).toDouble() / entry.duration, 1.0)},
            };
        }
        return cuts;
    }
    }
    return {};
}

QHash<int, QByteArray> VideoFolderModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {UrlRole, "url"},
        {SizeRole, "size"},
        {QueueStateRole, "queueState"},
        {IsFolderRole, "isFolder"},
        {ModifiedRole, "modified"},
        {ThumbnailRole, "thumbnail"},
        {DurationRole, "duration"},
        {CutsRole, "cuts"},
        {ItemCountRole, "itemCount"},
    };
}

QVariantMap VideoFolderModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

void VideoFolderModel::setFolder(const QUrl &folder)
{
    if (folder == m_folder)
        return;
    m_folder = folder;
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (!folder.toLocalFile().isEmpty())
        m_watcher.addPath(folder.toLocalFile());
    emit folderChanged();
    refresh();
}

QString VideoFolderModel::folderName() const
{
    const QDir dir(m_folder.toLocalFile());
    return dir.isRoot() ? QDir::toNativeSeparators(dir.absolutePath()) : dir.dirName();
}

QString VideoFolderModel::folderPath() const
{
    QString path = QDir(m_folder.toLocalFile()).absolutePath();
    const QString home = QDir::homePath();
    if (path == home || path.startsWith(home + QLatin1Char('/')))
        path = QLatin1Char('~') + path.mid(home.size());
    return QDir::toNativeSeparators(path);
}

QUrl VideoFolderModel::parentFolder() const
{
    const QString path = m_folder.toLocalFile();
    if (path.isEmpty())
        return {};
    QDir dir(path);
    if (dir.isRoot() || !dir.cdUp())
        return {};
    return QUrl::fromLocalFile(dir.absolutePath());
}

int VideoFolderModel::videoCount() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return !e.isDir; }));
}

qint64 VideoFolderModel::totalSize() const
{
    qint64 total = 0;
    for (const Entry &e : m_entries)
        total += e.isDir ? 0 : e.size;
    return total;
}

int VideoFolderModel::queuedCount() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [this](const Entry &e) {
        const int state = queueState(e);
        return state == TrimJob::Idle || state == TrimJob::Running;
    }));
}

int VideoFolderModel::trimmedCount() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(),
                             [this](const Entry &e) { return queueState(e) == TrimJob::Finished; }));
}

void VideoFolderModel::setFilter(Filter filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

void VideoFolderModel::setSort(Sort sort)
{
    if (sort == m_sort)
        return;
    m_sort = sort;
    emit sortChanged();
    rebuild();
}

void VideoFolderModel::setSearch(const QString &search)
{
    if (search == m_search)
        return;
    m_search = search;
    emit searchChanged();
    rebuild();
}

void VideoFolderModel::setQueue(ExportQueue *queue)
{
    if (queue == m_queue)
        return;
    if (m_queue)
        m_queue->disconnect(this);
    m_queue = queue;
    if (m_queue)
        connect(m_queue, &ExportQueue::sourceChanged, this, &VideoFolderModel::onQueueSourceChanged);
    emit queueChanged();
    rebuild();
}

void VideoFolderModel::refresh()
{
    m_refreshTimer.stop();
    stopThumbnails();
    QList<Entry> entries;
    const QString path = m_folder.toLocalFile();
    if (!path.isEmpty()) {
        const QDir dir(path);
        const QStringList filters = videoFilters();
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
        for (const QFileInfo &info : dirs) {
            Entry e;
            e.name = info.fileName();
            e.path = info.absoluteFilePath();
            e.modified = info.lastModified();
            e.isDir = true;
            e.itemCount = int(QDir(e.path).entryList(filters, QDir::Files).size());
            entries.append(e);
        }
        const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
        for (const QFileInfo &info : files) {
            Entry e;
            e.name = info.fileName();
            e.path = info.absoluteFilePath();
            e.size = info.size();
            e.modified = info.lastModified();
            // Thumbnails made before are on disk already.
            const QDir cache(MediaPreview::cacheDirFor(e.path));
            if (QFileInfo(cache.filePath(QStringLiteral("card.jpg"))).size() > 0)
                e.thumbnail = QUrl::fromLocalFile(cache.filePath(QStringLiteral("card.jpg"))).toString();
            QFile duration(cache.filePath(QStringLiteral("card-duration.txt")));
            if (duration.open(QIODevice::ReadOnly)) {
                bool ok = false;
                const double d = duration.readAll().trimmed().toDouble(&ok);
                if (ok)
                    e.duration = d;
            }
            e.thumbTried = !e.thumbnail.isEmpty() && e.duration >= 0;
            entries.append(e);
        }
    }
    m_entries = entries;
    rebuild();
    startThumbnails();
}

bool VideoFolderModel::shown(const Entry &entry) const
{
    if (!m_search.isEmpty() && !entry.name.contains(m_search, Qt::CaseInsensitive))
        return false;
    if (m_filter == All)
        return true;
    if (entry.isDir)
        return false;
    const int state = queueState(entry);
    if (m_filter == Queued)
        return state == TrimJob::Idle || state == TrimJob::Running;
    return state == TrimJob::Finished;
}

void VideoFolderModel::rebuild()
{
    QList<int> folders, videos;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (shown(m_entries.at(i)))
            (m_entries.at(i).isDir ? folders : videos).append(i);
    }
    const auto byName = [this](int a, int b) { return naturalLess(m_entries.at(a).name, m_entries.at(b).name); };
    std::sort(folders.begin(), folders.end(), byName);
    switch (m_sort) {
    case Newest:
    case Oldest:
        std::stable_sort(videos.begin(), videos.end(), [this, byName](int a, int b) {
            const QDateTime &ma = m_entries.at(a).modified, &mb = m_entries.at(b).modified;
            if (ma == mb)
                return byName(a, b);
            return m_sort == Newest ? ma > mb : ma < mb;
        });
        break;
    case Name:
        std::sort(videos.begin(), videos.end(), byName);
        break;
    case Largest:
        std::stable_sort(videos.begin(), videos.end(), [this, byName](int a, int b) {
            if (m_entries.at(a).size == m_entries.at(b).size)
                return byName(a, b);
            return m_entries.at(a).size > m_entries.at(b).size;
        });
        break;
    }

    QVariantList groups;
    const auto addGroup = [&groups](const QString &title, int start, int count) {
        groups << QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("start"), start}, {QStringLiteral("count"), count}};
    };
    if (!folders.isEmpty())
        addGroup(QStringLiteral("Folders"), 0, int(folders.size()));
    const int first = int(folders.size());
    if (m_sort == Newest || m_sort == Oldest) {
        int start = 0;
        for (int i = 1; i <= videos.size(); ++i) {
            if (i == videos.size()
                || m_entries.at(videos.at(i)).modified.date() != m_entries.at(videos.at(start)).modified.date()) {
                addGroup(dayTitle(m_entries.at(videos.at(start)).modified.date()), first + start, i - start);
                start = i;
            }
        }
    } else if (!videos.isEmpty()) {
        addGroup(QStringLiteral("Videos"), first, int(videos.size()));
    }

    beginResetModel();
    m_rows = folders + videos;
    m_groups = groups;
    endResetModel();
    emit countChanged();
    emit statsChanged();
    ++m_revision;
    emit revisionChanged();
}

QUrl VideoFolderModel::urlAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return QUrl::fromLocalFile(m_entries.at(m_rows.at(row)).path);
}

int VideoFolderModel::indexOf(const QUrl &url) const
{
    return rowOfEntry(entryOfPath(url.toLocalFile()));
}

int VideoFolderModel::entryOfPath(const QString &path) const
{
    if (path.isEmpty())
        return -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == path)
            return i;
    }
    return -1;
}

int VideoFolderModel::rowOfEntry(int entry) const
{
    return entry < 0 ? -1 : int(m_rows.indexOf(entry));
}

bool VideoFolderModel::isFolder(const QUrl &url) const
{
    return url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir();
}

QUrl VideoFolderModel::firstVideo() const
{
    for (int entry : m_rows) {
        if (!m_entries.at(entry).isDir)
            return QUrl::fromLocalFile(m_entries.at(entry).path);
    }
    for (const Entry &e : m_entries) {
        if (!e.isDir)
            return QUrl::fromLocalFile(e.path);
    }
    return {};
}

QUrl VideoFolderModel::adjacentVideo(const QUrl &url, int delta) const
{
    QList<int> videos;
    for (int entry : m_rows) {
        if (!m_entries.at(entry).isDir)
            videos << entry;
    }
    const int at = int(videos.indexOf(entryOfPath(url.toLocalFile())));
    if (at < 0)
        return videos.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_entries.at(videos.first()).path);
    const int next = at + delta;
    if (next < 0 || next >= videos.size())
        return {};
    return QUrl::fromLocalFile(m_entries.at(videos.at(next)).path);
}

void VideoFolderModel::entryChanged(int entry, const QList<int> &roles)
{
    const int row = rowOfEntry(entry);
    if (row >= 0)
        emit dataChanged(index(row), index(row), roles);
    ++m_revision;
    emit revisionChanged();
}

void VideoFolderModel::onQueueSourceChanged(const QUrl &source)
{
    const int entry = entryOfPath(source.toLocalFile());
    if (entry < 0)
        return;
    // A finished trim may have written a video here, or deleted the original.
    const TrimJob *job = m_queue ? m_queue->jobFor(source) : nullptr;
    if (job && job->state() == TrimJob::Finished)
        m_refreshTimer.start();
    // Which videos a filter shows can change with the queue.
    if (m_filter != All) {
        rebuild();
        return;
    }
    entryChanged(entry, {QueueStateRole, CutsRole});
    emit statsChanged();
}

void VideoFolderModel::stopThumbnails()
{
    for (const QPointer<QProcess> &process : std::as_const(m_processes)) {
        if (process) {
            process->disconnect(this);
            process->kill();
            process->waitForFinished(500);
            process->deleteLater();
        }
    }
    m_processes.clear();
}

void VideoFolderModel::startThumbnails()
{
    if (TrimJob::ffmpegPath().isEmpty())
        return;
    m_processes.removeAll(QPointer<QProcess>());
    // Rows shown come first, so the top of the list fills in first.
    QList<int> order = m_rows;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!order.contains(i))
            order << i;
    }
    for (int entry : std::as_const(order)) {
        if (m_processes.size() >= kMaxThumbnailJobs)
            return;
        Entry &e = m_entries[entry];
        if (e.isDir || e.thumbTried)
            continue;
        e.thumbTried = true;
        launchThumbnail(entry, false);
    }
}

void VideoFolderModel::launchThumbnail(int entry, bool fromStart)
{
    const QString path = m_entries.at(entry).path;
    const QString cacheDir = MediaPreview::cacheDirFor(path);
    QDir().mkpath(cacheDir);
    // Written beside the cache and moved in once whole, so a cut-short run
    // never leaves half a thumbnail behind.
    const QString file = QDir(cacheDir).filePath(QStringLiteral("card.part.jpg"));
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_processes << process;
    connect(process, &QProcess::finished, this,
            [this, process, entry, file, fromStart] { thumbnailFinished(process, entry, file, fromStart); });
    connect(process, &QProcess::errorOccurred, this, [this, process, entry, file](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            thumbnailFinished(process, entry, file, true);
    });
    QStringList args{QStringLiteral("-hide_banner")};
    // A frame a second in skips the black first frame many videos have.
    if (!fromStart)
        args << QStringLiteral("-ss") << QStringLiteral("1");
    args << QStringLiteral("-i") << path << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-an") << QStringLiteral("-sn") << QStringLiteral("-vf")
         << QStringLiteral("scale=%1:-2").arg(kThumbnailWidth) << QStringLiteral("-q:v") << QStringLiteral("4")
         << QStringLiteral("-y") << file;
    process->start(TrimJob::ffmpegPath(), args);
}

void VideoFolderModel::thumbnailFinished(QProcess *process, int entry, const QString &file, bool fromStart)
{
    if (!m_processes.contains(process))
        return;
    m_processes.removeAll(process);
    process->deleteLater();
    if (entry >= m_entries.size())
        return;
    Entry &e = m_entries[entry];

    static const QRegularExpression durationPattern(QStringLiteral("Duration: (\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = durationPattern.match(QString::fromUtf8(process->readAll()));
    if (match.hasMatch()) {
        e.duration = match.captured(1).toInt() * 3600 + match.captured(2).toInt() * 60 + match.captured(3).toDouble();
        QFile cached(QDir(QFileInfo(file).path()).filePath(QStringLiteral("card-duration.txt")));
        if (cached.open(QIODevice::WriteOnly))
            cached.write(QByteArray::number(e.duration, 'f', 3));
    }
    const bool made = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0
        && QFileInfo(file).size() > 0;
    const QString final = QDir(QFileInfo(file).path()).filePath(QStringLiteral("card.jpg"));
    if (made) {
        QFile::remove(final);
        QFile::rename(file, final);
        e.thumbnail = QUrl::fromLocalFile(final).toString();
        entryChanged(entry, {ThumbnailRole, DurationRole, CutsRole});
    } else if (!fromStart && process->error() != QProcess::FailedToStart) {
        // Shorter than a second: take the first frame instead.
        QFile::remove(file);
        launchThumbnail(entry, true);
        return;
    } else {
        entryChanged(entry, {DurationRole, CutsRole});
    }
    startThumbnails();
}
