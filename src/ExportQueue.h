#pragma once

#include "Settings.h"
#include "TrimJob.h"

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// The export queue: one TrimJob per queued video, run a few at a
// time, each writing its own output file.
class ExportQueue : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY statsChanged)
    Q_PROPERTY(int finishedCount READ finishedCount NOTIFY statsChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY statsChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY maxConcurrentChanged)
    // Output and encoder options are read from here as each job starts.
    Q_PROPERTY(Settings *settings READ settings WRITE setSettings NOTIFY settingsChanged)

public:
    enum Roles {
        JobRole = Qt::UserRole + 1,
        SourceRole,
        NameRole,
        InPointRole,
        OutPointRole,
        ModeRole,
        StateRole,
        ProgressRole,
        OutputRole,
        ErrorRole,
        ErrorDetailsRole,
        SegmentCountRole,
        KeptDurationRole,
        OutputNameRole,
        DefaultNameRole,
        ExtensionRole,
    };

    explicit ExportQueue(QObject *parent = nullptr);
    ~ExportQueue() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_jobs.size()); }
    int pendingCount() const { return countIn(TrimJob::Idle); }
    int finishedCount() const { return countIn(TrimJob::Finished); }
    int failedCount() const { return countIn(TrimJob::Failed); }
    bool running() const { return m_running; }
    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int value);
    Settings *settings() const { return m_settings; }
    void setSettings(Settings *settings);

    // Queues a trim. A video that is already waiting in the queue (or failed,
    // or was cancelled) gets its cut updated instead of a second entry. With
    // two or more segments ({inPoint, outPoint} objects) they are joined.
    Q_INVOKABLE void add(const QUrl &source, double inPoint, double outPoint, int mode,
                         const QVariantList &segments = {});
    Q_INVOKABLE void remove(int row);
    // Names a job's output file; see TrimJob::outputName. Empty goes back to
    // the filename pattern.
    Q_INVOKABLE void setOutputName(int row, const QString &name);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

    // The most recent job for a video, or null if it was never queued.
    Q_INVOKABLE TrimJob *jobFor(const QUrl &source) const;

signals:
    void countChanged();
    void statsChanged();
    void runningChanged();
    void maxConcurrentChanged();
    void settingsChanged();
    // A job for this video was added, removed or changed state.
    void sourceChanged(const QUrl &source);
    void allFinished();
    // Forwarded from the jobs; see TrimJob.
    void aboutToDeleteOriginal(const QUrl &source);
    void deleteOriginalFinished(const QUrl &source, const QUrl &current, bool deleted);

private:
    int countIn(TrimJob::State state) const;
    // What the filename pattern names a job's output, without the folder,
    // the _2 for a taken name, or the extension.
    QString defaultName(const TrimJob *job) const;
    void refreshDefaultNames();
    int rowOf(const TrimJob *job) const;
    void onJobChanged(TrimJob *job, const QList<int> &roles);
    void scheduleNext();
    void setRunning(bool running);

    QList<TrimJob *> m_jobs;
    bool m_running = false;
    int m_maxConcurrent = 2;
    QPointer<Settings> m_settings;
};
