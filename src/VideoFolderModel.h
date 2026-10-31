#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class ExportQueue;
class QProcess;

// One folder as the file browser shows it: its subfolders, then its videos,
// filtered, searched and sorted, in groups (by day when sorted by date).
// Each video has a thumbnail and duration, made with FFmpeg in the
// background and cached, and its state in the export queue.
class VideoFolderModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY folderChanged)
    // The folder's path for showing, with ~ for the home folder.
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY folderChanged)
    // The folder above, or empty at the top of the drive.
    Q_PROPERTY(QUrl parentFolder READ parentFolder NOTIFY folderChanged)
    // Rows shown, folders included.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Every video in the folder, whatever the filter.
    Q_PROPERTY(int videoCount READ videoCount NOTIFY countChanged)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY countChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY statsChanged)
    Q_PROPERTY(int trimmedCount READ trimmedCount NOTIFY statsChanged)
    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(Sort sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    // [{title, start, count}] over the rows, in order.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY countChanged)
    // Goes up whenever any row's data changes, for bindings that use get().
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(ExportQueue *queue READ queue WRITE setQueue NOTIFY queueChanged)

public:
    enum Filter { All, Queued, Trimmed };
    Q_ENUM(Filter)
    enum Sort { Newest, Oldest, Name, Largest };
    Q_ENUM(Sort)

    enum Roles {
        NameRole = Qt::UserRole + 1,
        UrlRole,
        SizeRole,
        // -1 when the video is not in the queue, otherwise a TrimJob::State.
        QueueStateRole,
        IsFolderRole,
        ModifiedRole,
        // File URL of the thumbnail, or empty until it is made.
        ThumbnailRole,
        // Seconds, or -1 until known.
        DurationRole,
        // Parts the queued trim keeps, as {start, end} fractions of the video.
        CutsRole,
        // Videos inside a subfolder.
        ItemCountRole,
    };

    explicit VideoFolderModel(QObject *parent = nullptr);
    ~VideoFolderModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QUrl folder() const { return m_folder; }
    void setFolder(const QUrl &folder);
    QString folderName() const;
    QString folderPath() const;
    QUrl parentFolder() const;
    int count() const { return int(m_rows.size()); }
    int videoCount() const;
    qint64 totalSize() const;
    int queuedCount() const;
    int trimmedCount() const;
    Filter filter() const { return m_filter; }
    void setFilter(Filter filter);
    Sort sort() const { return m_sort; }
    void setSort(Sort sort);
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QVariantList groups() const { return m_groups; }
    int revision() const { return m_revision; }
    ExportQueue *queue() const { return m_queue; }
    void setQueue(ExportQueue *queue);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QUrl urlAt(int row) const;
    Q_INVOKABLE int indexOf(const QUrl &url) const;
    Q_INVOKABLE bool isFolder(const QUrl &url) const;
    // Every role of one row, by role name.
    Q_INVOKABLE QVariantMap get(int row) const;
    // The first video shown, or the first in the folder if none are.
    Q_INVOKABLE QUrl firstVideo() const;
    // The video delta places from url among those shown.
    Q_INVOKABLE QUrl adjacentVideo(const QUrl &url, int delta) const;

    static const QStringList &videoExtensions();

signals:
    void folderChanged();
    void countChanged();
    void statsChanged();
    void filterChanged();
    void sortChanged();
    void searchChanged();
    void revisionChanged();
    void queueChanged();

private:
    struct Entry {
        QString name;
        QString path;
        qint64 size = 0;
        QDateTime modified;
        bool isDir = false;
        int itemCount = 0;
        QString thumbnail;
        double duration = -1;
        bool thumbTried = false;
    };

    int queueState(const Entry &entry) const;
    bool shown(const Entry &entry) const;
    void rebuild();
    int rowOfEntry(int entry) const;
    int entryOfPath(const QString &path) const;
    void entryChanged(int entry, const QList<int> &roles);
    void onQueueSourceChanged(const QUrl &source);
    void startThumbnails();
    void stopThumbnails();
    void launchThumbnail(int entry, bool fromStart);
    void thumbnailFinished(QProcess *process, int entry, const QString &file, bool fromStart);

    QUrl m_folder;
    QList<Entry> m_entries;
    // Indexes into m_entries, in the order shown.
    QList<int> m_rows;
    QVariantList m_groups;
    Filter m_filter = All;
    Sort m_sort = Name;
    QString m_search;
    int m_revision = 0;
    QPointer<ExportQueue> m_queue;
    QList<QPointer<QProcess>> m_processes;
    // New and removed files show up on their own, a moment after.
    QFileSystemWatcher m_watcher;
    QTimer m_refreshTimer;
};
