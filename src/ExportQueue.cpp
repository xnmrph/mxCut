#include "ExportQueue.h"

#include <QFileInfo>

ExportQueue::ExportQueue(QObject *parent)
    : QAbstractListModel(parent)
{
}

ExportQueue::~ExportQueue()
{
    // Jobs are children, but cancel first so no FFmpeg process outlives the app.
    for (TrimJob *job : std::as_const(m_jobs)) {
        job->disconnect(this);
        job->cancel();
    }
}

int ExportQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_jobs.size());
}

QVariant ExportQueue::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    TrimJob *job = m_jobs.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QFileInfo(job->source().toLocalFile()).fileName();
    case JobRole:
        return QVariant::fromValue(job);
    case SourceRole:
        return job->source();
    case InPointRole:
        return job->inPoint();
    case OutPointRole:
        return job->outPoint();
    case ModeRole:
        return int(job->mode());
    case StateRole:
        return int(job->state());
    case ProgressRole:
        return job->progress();
    case OutputRole:
        return job->output();
    case ErrorRole:
        return job->errorString();
    case ErrorDetailsRole:
        return job->errorDetails();
    case SegmentCountRole:
        return qMax<qsizetype>(1, job->segments().size());
    case KeptDurationRole:
        return job->keptDuration();
    case OutputNameRole:
        return job->outputName();
    case DefaultNameRole:
        return defaultName(job);
    case ExtensionRole:
        return QFileInfo(TrimJob::outputPathFor(job->source().toLocalFile(), job->mode(), {})).suffix();
    }
    return {};
}

QHash<int, QByteArray> ExportQueue::roleNames() const
{
    return {
        {JobRole, "job"},
        {SourceRole, "source"},
        {NameRole, "name"},
        {InPointRole, "inPoint"},
        {OutPointRole, "outPoint"},
        {ModeRole, "mode"},
        {StateRole, "jobState"},
        {ProgressRole, "progress"},
        {OutputRole, "output"},
        {ErrorRole, "errorString"},
        {ErrorDetailsRole, "errorDetails"},
        {SegmentCountRole, "segmentCount"},
        {KeptDurationRole, "keptDuration"},
        {OutputNameRole, "outputName"},
        {DefaultNameRole, "defaultName"},
        {ExtensionRole, "extension"},
    };
}

void ExportQueue::setMaxConcurrent(int value)
{
    value = qBound(1, value, 16);
    if (value == m_maxConcurrent)
        return;
    m_maxConcurrent = value;
    emit maxConcurrentChanged();
    if (m_running)
        scheduleNext();
}

void ExportQueue::setSettings(Settings *settings)
{
    if (settings == m_settings)
        return;
    if (m_settings)
        m_settings->disconnect(this);
    m_settings = settings;
    // The default names follow the filename pattern as it is changed.
    if (m_settings)
        connect(m_settings, &Settings::changed, this, &ExportQueue::refreshDefaultNames);
    refreshDefaultNames();
    emit settingsChanged();
}

void ExportQueue::add(const QUrl &source, double inPoint, double outPoint, int mode, const QVariantList &segments)
{
    TrimJob *job = jobFor(source);
    const bool reuse = job && job->state() != TrimJob::Running && job->state() != TrimJob::Finished;

    if (!reuse) {
        job = new TrimJob(this);
        job->setSource(source);
        connect(job, &TrimJob::stateChanged, this, [this, job] {
            onJobChanged(job, {StateRole, OutputRole, ErrorRole, ErrorDetailsRole});
        });
        connect(job, &TrimJob::progressChanged, this, [this, job] { onJobChanged(job, {ProgressRole}); });
        connect(job, &TrimJob::outputChanged, this, [this, job] { onJobChanged(job, {OutputRole}); });
        connect(job, &TrimJob::finished, this, &ExportQueue::scheduleNext, Qt::QueuedConnection);
        connect(job, &TrimJob::aboutToDeleteOriginal, this, &ExportQueue::aboutToDeleteOriginal);
        connect(job, &TrimJob::deleteOriginalFinished, this, &ExportQueue::deleteOriginalFinished);
    }

    job->reset();
    job->setInPoint(inPoint);
    job->setOutPoint(outPoint);
    job->setSegments(segments.size() >= 2 ? segments : QVariantList{});
    job->setMode(TrimJob::Mode(mode));

    if (reuse) {
        onJobChanged(job, {InPointRole, OutPointRole, ModeRole, StateRole, ProgressRole, OutputRole, ErrorRole,
                           ErrorDetailsRole, SegmentCountRole, KeptDurationRole, DefaultNameRole,
                           ExtensionRole});
    } else {
        beginInsertRows({}, int(m_jobs.size()), int(m_jobs.size()));
        m_jobs.append(job);
        endInsertRows();
        emit countChanged();
        emit statsChanged();
        emit sourceChanged(source);
    }

    if (m_running)
        scheduleNext();
}

void ExportQueue::remove(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    TrimJob *job = m_jobs.at(row);
    const QUrl source = job->source();
    job->disconnect(this);
    job->cancel();

    beginRemoveRows({}, row, row);
    m_jobs.removeAt(row);
    endRemoveRows();
    job->deleteLater();

    emit countChanged();
    emit statsChanged();
    emit sourceChanged(source);
    if (m_running)
        scheduleNext();
}

void ExportQueue::setOutputName(int row, const QString &name)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    TrimJob *job = m_jobs.at(row);
    if (job->state() == TrimJob::Running || job->outputName() == name)
        return;
    job->setOutputName(name);
    onJobChanged(job, {OutputNameRole});
}

void ExportQueue::clearFinished()
{
    for (int row = int(m_jobs.size()) - 1; row >= 0; --row) {
        if (m_jobs.at(row)->state() == TrimJob::Finished)
            remove(row);
    }
}

void ExportQueue::start()
{
    if (m_running)
        return;
    // Failed and cancelled jobs get another go when the queue is started again.
    for (TrimJob *job : std::as_const(m_jobs)) {
        if (job->state() == TrimJob::Failed || job->state() == TrimJob::Cancelled)
            job->reset();
    }
    setRunning(true);
    scheduleNext();
}

void ExportQueue::cancel()
{
    setRunning(false);
    for (TrimJob *job : std::as_const(m_jobs))
        job->cancel();
}

TrimJob *ExportQueue::jobFor(const QUrl &source) const
{
    for (auto it = m_jobs.crbegin(); it != m_jobs.crend(); ++it) {
        if ((*it)->source() == source)
            return *it;
    }
    return nullptr;
}

int ExportQueue::countIn(TrimJob::State state) const
{
    return int(std::count_if(m_jobs.cbegin(), m_jobs.cend(),
                             [state](const TrimJob *job) { return job->state() == state; }));
}

QString ExportQueue::defaultName(const TrimJob *job) const
{
    TrimOptions options = m_settings ? m_settings->trimOptions() : TrimOptions{};
    options.deleteOriginal = false;
    options.overwrite = true;
    options.outputFolder = QStringLiteral("/nonexistent-preview-folder");
    const QString path = TrimJob::outputPathFor(job->source().toLocalFile(), job->mode(), options,
                                                job->inPoint(), job->outPoint());
    return QFileInfo(path).completeBaseName();
}

void ExportQueue::refreshDefaultNames()
{
    if (!m_jobs.isEmpty())
        emit dataChanged(index(0), index(int(m_jobs.size()) - 1), {DefaultNameRole});
}

int ExportQueue::rowOf(const TrimJob *job) const
{
    return int(m_jobs.indexOf(const_cast<TrimJob *>(job)));
}

void ExportQueue::onJobChanged(TrimJob *job, const QList<int> &roles)
{
    const int row = rowOf(job);
    if (row < 0)
        return;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    if (roles.contains(StateRole)) {
        emit statsChanged();
        emit sourceChanged(job->source());
    }
}

void ExportQueue::scheduleNext()
{
    if (!m_running)
        return;

    int active = countIn(TrimJob::Running);
    for (TrimJob *job : std::as_const(m_jobs)) {
        if (active >= m_maxConcurrent)
            break;
        if (job->state() == TrimJob::Idle) {
            job->applySettings(m_settings);
            job->start();
            if (job->state() == TrimJob::Running)
                ++active;
        }
    }

    if (active == 0 && pendingCount() == 0) {
        setRunning(false);
        emit allFinished();
    }
}

void ExportQueue::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    emit runningChanged();
}
