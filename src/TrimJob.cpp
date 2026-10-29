#include "TrimJob.h"

#include "Encoders.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr qsizetype kStderrTailBytes = 4096;
// While deleting the original, how often and how long to wait for it to be
// let go of (Windows can't delete a file another program has open).
constexpr int kDeleteRetryMs = 250;
constexpr int kDeleteTries = 16;

QString seconds(double value)
{
    return QString::number(value, 'f', 6);
}

QString outputSuffix(const QString &input, TrimJob::Mode mode)
{
    const QString suffix = QFileInfo(input).suffix().toLower();
    if (mode == TrimJob::Fast)
        return suffix.isEmpty() ? QStringLiteral("mkv") : suffix;
    // Re-encoded H.264/HEVC/AV1 fits in MKV or MP4 but not in containers such
    // as WebM or AVI.
    return suffix == QLatin1String("mkv") ? suffix : QStringLiteral("mp4");
}

// 83.5 s -> "00-01-23.500", safe in a filename on every platform.
QString fileTime(double t)
{
    const qint64 ms = qRound64(qMax(0.0, t) * 1000);
    return QStringLiteral("%1-%2-%3.%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString sanitizeFileName(QString name)
{
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    for (QChar &c : name) {
        if (invalid.contains(c) || c.unicode() < 32)
            c = QLatin1Char('_');
    }
    name = name.trimmed();
    while (name.endsWith(QLatin1Char('.')))
        name.chop(1);
    return name;
}

QString ffmpegOverride;

// QFileInfo's operator== treats any two missing files as equal, so compare
// paths unless both files exist.
bool sameFile(const QString &a, const QString &b)
{
    const QFileInfo fa(a), fb(b);
    if (fa.exists() && fb.exists())
        return fa.canonicalFilePath() == fb.canonicalFilePath();
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(fa.absoluteFilePath()).compare(QDir::cleanPath(fb.absoluteFilePath()), cs) == 0;
}

// Whether the file has an audio stream, from FFmpeg's description of it.
bool hasAudio(const QString &ffmpeg, const QString &input)
{
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-i"), input});
    if (!p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished(2000);
        return true;
    }
    static const QRegularExpression audio(QStringLiteral("Stream #\\d+:\\d+.*: Audio:"));
    return audio.match(QString::fromUtf8(p.readAllStandardError())).hasMatch();
}

// Whether FFmpeg can open the file and finds a video stream with a length in
// it: checked before the original is deleted.
bool isPlayableVideo(const QString &ffmpeg, const QString &path)
{
    if (QFileInfo(path).size() <= 0)
        return false;
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-i"), path});
    if (!p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished(2000);
        return false;
    }
    const QString out = QString::fromUtf8(p.readAllStandardError());
    static const QRegularExpression video(QStringLiteral("Stream #\\d+:\\d+.*: Video:"));
    static const QRegularExpression duration(QStringLiteral("Duration: \\d+:\\d+:\\d+"));
    return video.match(out).hasMatch() && duration.match(out).hasMatch();
}

} // namespace

TrimJob::TrimJob(QObject *parent)
    : QObject(parent)
{
}

TrimJob::~TrimJob()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void TrimJob::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    emit sourceChanged();
}

void TrimJob::setOutput(const QString &output)
{
    if (output == m_output)
        return;
    m_output = output;
    emit outputChanged();
}

void TrimJob::setOutputName(const QString &name)
{
    if (name == m_outputName)
        return;
    m_outputName = name;
    emit outputNameChanged();
}

void TrimJob::setInPoint(double seconds)
{
    if (qFuzzyCompare(seconds, m_inPoint))
        return;
    m_inPoint = seconds;
    emit inPointChanged();
}

void TrimJob::setOutPoint(double seconds)
{
    if (qFuzzyCompare(seconds, m_outPoint))
        return;
    m_outPoint = seconds;
    emit outPointChanged();
}

QVariantList TrimJob::segments() const
{
    QVariantList list;
    for (const Range &r : m_segments)
        list << QVariantMap{{QStringLiteral("inPoint"), r.in}, {QStringLiteral("outPoint"), r.out}};
    return list;
}

void TrimJob::setSegments(const QVariantList &segments)
{
    QList<Range> ranges;
    for (const QVariant &v : segments) {
        const QVariantMap m = v.toMap();
        ranges.append({m.value(QStringLiteral("inPoint")).toDouble(), m.value(QStringLiteral("outPoint")).toDouble()});
    }
    std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.in < b.in; });
    m_segments = ranges;
    // The overall span names the file ({in} and {out}) and shows in the queue.
    if (!ranges.isEmpty()) {
        double first = ranges.first().in, last = ranges.first().out;
        for (const Range &r : ranges)
            last = qMax(last, r.out);
        setInPoint(first);
        setOutPoint(last);
    }
    emit segmentsChanged();
}

QList<TrimJob::Range> TrimJob::ranges() const
{
    if (m_segments.size() >= 2)
        return m_segments;
    return {{m_inPoint, m_outPoint}};
}

double TrimJob::keptDuration() const
{
    double total = 0;
    for (const Range &r : ranges())
        total += qMax(0.0, r.out - r.in);
    return total;
}

void TrimJob::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit modeChanged();
}

void TrimJob::setFfmpegOverride(const QString &path)
{
    ffmpegOverride = path;
}

void TrimJob::applySettings(Settings *settings)
{
    if (settings)
        m_options = settings->trimOptions();
}

QString TrimJob::ffmpegPath()
{
    if (!ffmpegOverride.isEmpty()) {
        const QFileInfo info(ffmpegOverride);
        return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
    }
    const QString exe = QStringLiteral("ffmpeg");
    const QString bundled = QStandardPaths::findExecutable(exe, {QCoreApplication::applicationDirPath()});
    if (!bundled.isEmpty())
        return bundled;
    return QStandardPaths::findExecutable(exe);
}

QString TrimJob::outputPathFor(const QString &input, Mode mode, const TrimOptions &options,
                               double inPoint, double outPoint)
{
    const QFileInfo info(input);
    const QString dir = options.outputFolder.isEmpty() ? info.absolutePath() : options.outputFolder;
    const QDateTime now = QDateTime::currentDateTime();

    QString pattern = options.filenamePattern.trimmed();
    if (pattern.isEmpty())
        pattern = QStringLiteral("{name}_trim");
    QString base = pattern;
    base.replace(QLatin1String("{name}"), info.completeBaseName())
        .replace(QLatin1String("{in}"), fileTime(inPoint))
        .replace(QLatin1String("{out}"), fileTime(outPoint))
        .replace(QLatin1String("{mode}"), mode == Fast ? QStringLiteral("fast") : QStringLiteral("accurate"))
        .replace(QLatin1String("{date}"), now.toString(QStringLiteral("yyyy-MM-dd")))
        .replace(QLatin1String("{time}"), now.toString(QStringLiteral("HH-mm-ss")));
    base = sanitizeFileName(base);
    if (base.isEmpty())
        base = info.completeBaseName() + QStringLiteral("_trim");

    const QString suffix = outputSuffix(input, mode);
    QString candidate = QDir(dir).filePath(base + QLatin1Char('.') + suffix);
    // Never write over the source, even when overwriting is allowed, unless
    // it is being deleted: then the trim can have its name once it's gone.
    const auto taken = [&](const QString &path) {
        if (sameFile(path, input))
            return !options.deleteOriginal;
        return !options.overwrite && QFileInfo::exists(path);
    };
    for (int n = 2; taken(candidate); ++n)
        candidate = QDir(dir).filePath(QStringLiteral("%1_%2.%3").arg(base).arg(n).arg(suffix));
    return candidate;
}

QString TrimJob::friendlyError(const QString &out, const QString &encoder)
{
    const auto has = [&out](const char *text) { return out.contains(QLatin1String(text), Qt::CaseInsensitive); };

    if (has("Unknown encoder") || has("Encoder not found"))
        return tr("This FFmpeg doesn't include the %1 encoder. Choose another one in Settings.").arg(encoder);
    // Match encoder names and known hardware errors, not bare words, so a file
    // path that happens to contain "amf" or "qsv" isn't taken for a GPU error.
    if (has("_nvenc") || has("_qsv") || has("_amf") || has("_vaapi") || has("nvcuda")
        || has("CUDA_ERROR") || has("No capable devices") || has("Failed to initialise VAAPI")
        || has("MFX session") || has("Device creation failed") || has("hwupload"))
        return tr("The GPU encoder couldn't start on this machine. Choose a CPU encoder in Settings.");
    if (has("No space left on device"))
        return tr("The disk is full.");
    if (has("Permission denied"))
        return tr("Permission denied while reading the video or writing the trimmed file.");
    if (has("Invalid data found when processing input") || has("moov atom not found")
        || has("could not find codec parameters"))
        return tr("FFmpeg can't read this file. It may be damaged or not a video.");
    if (has("No such file or directory"))
        return tr("The video or the output folder no longer exists.");
    if (has("Could not find tag for codec") || has("not currently supported in container")
        || has("codec not currently supported"))
        return tr("This video's streams can't be stored in the output file type. Try Accurate mode, "
                  "or turn off copying audio in Settings.");
    if (has("Error while opening encoder") || has("Error initializing output stream"))
        return tr("The %1 encoder couldn't start with these settings. Try another encoder in Settings.")
            .arg(encoder);

    const QStringList lines = out.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

namespace {
bool isMp4Like(const QString &path)
{
    return path.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive)
           || path.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive)
           || path.endsWith(QLatin1String(".m4v"), Qt::CaseInsensitive);
}

const QStringList progressArgs{
    QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
    QStringLiteral("-nostats"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
};
} // namespace

QStringList TrimJob::arguments() const
{
    return argumentsFor({m_inPoint, m_outPoint}, m_output);
}

QStringList TrimJob::argumentsFor(const Range &range, const QString &output) const
{
    const QString input = m_source.isLocalFile() ? m_source.toLocalFile() : m_source.toString();
    const bool mkv = output.endsWith(QLatin1String(".mkv"), Qt::CaseInsensitive);
    const bool mp4Like = isMp4Like(output);

    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-nostats"), QStringLiteral("-progress"), QStringLiteral("pipe:1"),
    };
    if (m_mode == Accurate)
        args << Encoders::inputArgs(m_options.videoEncoder);
    args << QStringList{
        // Seeking before -i is fast, and frame-exact when re-encoding.
        QStringLiteral("-ss"), seconds(range.in),
        QStringLiteral("-i"), input,
        QStringLiteral("-t"), seconds(range.out - range.in),
        // Capital V skips cover art, which is stored as a video stream.
        QStringLiteral("-map"), QStringLiteral("0:V?"),
        QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-map_metadata"), QStringLiteral("0"),
    };

    if (m_mode == Fast) {
        args << QStringLiteral("-map") << QStringLiteral("0:s?")
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero");
    } else {
        args << Encoders::videoArgs(m_options.videoEncoder, m_options.quality, m_options.speed, mp4Like);
        if (m_options.copyAudio)
            args << QStringLiteral("-c:a") << QStringLiteral("copy");
        else
            args << QStringLiteral("-c:a") << QStringLiteral("aac")
                 << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(m_options.audioBitrate);
        // MKV takes any subtitle format as-is; MP4 cannot hold most of them.
        if (mkv)
            args << QStringLiteral("-map") << QStringLiteral("0:s?") << QStringLiteral("-c:s") << QStringLiteral("copy");
        else
            args << QStringLiteral("-sn");
    }

    if (mp4Like)
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");

    args << output;
    return args;
}

// Accurate mode with several parts: each part is its own input, and FFmpeg's
// concat filter joins them before encoding, all in one run.
QStringList TrimJob::joinedAccurateArguments(const QList<Range> &ranges, bool withAudio) const
{
    const QString input = m_source.isLocalFile() ? m_source.toLocalFile() : m_source.toString();
    QStringList args = progressArgs;
    args << Encoders::inputArgs(m_options.videoEncoder);
    QString graph;
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        args << QStringLiteral("-ss") << seconds(ranges[i].in)
             << QStringLiteral("-t") << seconds(ranges[i].out - ranges[i].in)
             << QStringLiteral("-i") << input;
        graph += QStringLiteral("[%1:v:0]").arg(i);
        if (withAudio)
            graph += QStringLiteral("[%1:a:0]").arg(i);
    }
    graph += QStringLiteral("concat=n=%1:v=1:a=%2[v]").arg(ranges.size()).arg(withAudio ? 1 : 0);
    if (withAudio)
        graph += QStringLiteral("[a]");

    // An encoder's own video filter (GPU upload) moves to the end of the graph.
    QStringList video = Encoders::videoArgs(m_options.videoEncoder, m_options.quality, m_options.speed,
                                            isMp4Like(m_output));
    const qsizetype vf = video.indexOf(QStringLiteral("-vf"));
    QString videoOut = QStringLiteral("[v]");
    if (vf >= 0 && vf + 1 < video.size()) {
        graph += QStringLiteral(";[v]%1[vf]").arg(video[vf + 1]);
        videoOut = QStringLiteral("[vf]");
        video.remove(vf, 2);
    }

    args << QStringLiteral("-filter_complex") << graph
         << QStringLiteral("-map") << videoOut;
    if (withAudio)
        args << QStringLiteral("-map") << QStringLiteral("[a]");
    args << QStringLiteral("-map_metadata") << QStringLiteral("0") << video;
    // Joined audio is always re-encoded; it can't be copied through a filter.
    if (withAudio)
        args << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(m_options.audioBitrate);
    args << QStringLiteral("-sn");
    if (isMp4Like(m_output))
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    args << m_output;
    return args;
}

// Fast mode with several parts: the copied parts are joined without
// re-encoding by FFmpeg's concat demuxer.
QStringList TrimJob::concatArguments(const QString &listFile) const
{
    QStringList args = progressArgs;
    args << QStringLiteral("-f") << QStringLiteral("concat") << QStringLiteral("-safe") << QStringLiteral("0")
         << QStringLiteral("-i") << listFile
         << QStringLiteral("-map") << QStringLiteral("0")
         << QStringLiteral("-c") << QStringLiteral("copy");
    if (isMp4Like(m_output))
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    args << m_output;
    return args;
}

void TrimJob::start()
{
    if (m_state == Running)
        return;

    setProgress(0);
    setErrorString({});
    m_stdoutBuffer.clear();
    m_stderrTail.clear();

    const QString input = m_source.toLocalFile();
    const QFileInfo inputInfo(input);
    if (input.isEmpty() || !inputInfo.exists())
        return fail(tr("The video could not be found."));
    if (!inputInfo.isReadable())
        return fail(tr("The video can't be read. Check its permissions."));
    const QList<Range> parts = ranges();
    for (const Range &r : parts) {
        if (r.out - r.in <= 0)
            return fail(tr("The out point must be after the in point."));
    }
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty())
        return fail(tr("FFmpeg was not found. Install it, or set its location in Settings."));
    if (m_mode == Accurate && !Encoders::find(m_options.videoEncoder))
        return fail(tr("Unknown encoder \"%1\". Choose another one in Settings.").arg(m_options.videoEncoder));

    if (m_output.isEmpty()) {
        TrimOptions options = m_options;
        QString name = m_outputName.trimmed();
        // Someone typing "clip.mp4" means clip.mp4, not clip.mp4.mp4.
        const QString extension = QLatin1Char('.') + outputSuffix(input, m_mode);
        if (name.endsWith(extension, Qt::CaseInsensitive))
            name.chop(extension.size());
        if (!name.isEmpty())
            options.filenamePattern = name;
        setOutput(outputPathFor(input, m_mode, options, m_inPoint, m_outPoint));
    }
    m_finalOutput.clear();
    if (sameFile(m_output, input)) {
        if (!m_options.deleteOriginal)
            return fail(tr("The trimmed file would replace the original. Change the filename pattern in Settings."));
        // Written beside the original first, and renamed once it's gone.
        const QFileInfo info(m_output);
        const QString base = info.completeBaseName() + QStringLiteral(".mxcut-part");
        QString temporary = info.dir().filePath(base + QLatin1Char('.') + info.suffix());
        for (int n = 2; QFileInfo::exists(temporary); ++n)
            temporary = info.dir().filePath(QStringLiteral("%1%2.%3").arg(base).arg(n).arg(info.suffix()));
        m_finalOutput = m_output;
        setOutput(temporary);
    }
    const QFileInfo outputInfo(m_output);
    if (!QDir().mkpath(outputInfo.absolutePath()))
        return fail(tr("The output folder %1 can't be created.").arg(QDir::toNativeSeparators(outputInfo.absolutePath())));
    if (!QFileInfo(outputInfo.absolutePath()).isWritable())
        return fail(tr("Can't write to the output folder %1.").arg(QDir::toNativeSeparators(outputInfo.absolutePath())));

    m_ffmpeg = ffmpeg;
    m_steps.clear();
    m_step = 0;
    removeTemporaryFiles();
    const double total = keptDuration();
    if (parts.size() == 1) {
        m_steps.append({arguments(), 0, 1 / total});
    } else if (m_mode == Accurate) {
        m_steps.append({joinedAccurateArguments(parts, hasAudio(ffmpeg, input)), 0, 1 / total});
    } else {
        // Copy each part to a file of its own, then join them. The join is
        // quick, so it gets the last tenth of the progress bar.
        const QFileInfo out(m_output);
        QString list;
        double done = 0;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            const QString part = out.dir().filePath(QStringLiteral("%1.mxcut-part%2.%3")
                                                        .arg(out.completeBaseName()).arg(i + 1).arg(out.suffix()));
            m_temporaryFiles << part;
            m_steps.append({argumentsFor(parts[i], part), 0.9 * done / total, 0.9 / total});
            done += parts[i].out - parts[i].in;
            // The concat list quotes paths with single quotes; escape any in the path.
            QString quoted = QFileInfo(part).absoluteFilePath();
            quoted.replace(QLatin1String("'"), QLatin1String("'\\''"));
            list += QStringLiteral("file '%1'\n").arg(quoted);
        }
        const QString listFile = out.dir().filePath(out.completeBaseName() + QStringLiteral(".mxcut-list.txt"));
        QSaveFile f(listFile);
        if (!f.open(QIODevice::WriteOnly) || f.write(list.toUtf8()) < 0 || !f.commit())
            return fail(tr("Can't write to the output folder %1.").arg(QDir::toNativeSeparators(out.absolutePath())));
        m_temporaryFiles << listFile;
        m_steps.append({concatArguments(listFile), 0.9, 0.1 / total});
    }

    setState(Running);
    runStep();
}

void TrimJob::runStep()
{
    m_stdoutBuffer.clear();
    m_stderrTail.clear();
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &TrimJob::readProgress);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderrTail += m_process->readAllStandardError();
        if (m_stderrTail.size() > kStderrTailBytes)
            m_stderrTail = m_stderrTail.right(kStderrTailBytes);
    });
    connect(m_process, &QProcess::finished, this, &TrimJob::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onProcessFinished(-1, QProcess::CrashExit);
    });

    m_process->start(m_ffmpeg, m_steps[m_step].arguments);
}

void TrimJob::removeTemporaryFiles()
{
    for (const QString &path : std::as_const(m_temporaryFiles))
        QFile::remove(path);
    m_temporaryFiles.clear();
}

void TrimJob::cancel()
{
    if (m_state != Running || !m_process)
        return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(2000);
    m_process->deleteLater();
    QFile::remove(m_output);
    removeTemporaryFiles();
    setState(Cancelled);
    emit finished();
}

void TrimJob::reset()
{
    if (m_state == Running)
        return;
    setProgress(0);
    setErrorString({});
    setOutput({});
    setState(Idle);
}

QUrl TrimJob::outputFolderUrl() const
{
    return QUrl::fromLocalFile(QFileInfo(m_output).absolutePath());
}

void TrimJob::readProgress()
{
    m_stdoutBuffer += m_process->readAllStandardOutput();
    const Step &step = m_steps[m_step];
    qsizetype newline;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (line.startsWith("out_time_us=")) {
            bool ok = false;
            const qint64 us = line.mid(12).toLongLong(&ok);
            if (ok)
                setProgress(qBound(0.0, step.progressStart + us / 1e6 * step.progressPerSecond, 1.0));
        }
    }
}

void TrimJob::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_state != Running)
        return;
    if (m_process)
        m_process->deleteLater();

    if (status == QProcess::NormalExit && exitCode == 0 && m_step + 1 < m_steps.size()) {
        ++m_step;
        runStep();
        return;
    }
    removeTemporaryFiles();

    if (status == QProcess::NormalExit && exitCode == 0) {
        setProgress(1);
        if (m_options.deleteOriginal) {
            if (!isPlayableVideo(m_ffmpeg, m_output)) {
                keepOriginal(tr("The trimmed video couldn't be checked, so the original was kept."));
                return;
            }
            m_deleteTries = 0;
            emit aboutToDeleteOriginal(m_source);
            // Give whoever had the original open a moment to close it.
            QTimer::singleShot(kDeleteRetryMs, this, &TrimJob::deleteOriginal);
            return;
        }
        setState(Finished);
    } else {
        const QString output = QString::fromUtf8(m_stderrTail).trimmed();
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString details = lines.mid(qMax(0, lines.size() - 6)).join(QLatin1Char('\n'));
        QString error = friendlyError(output, m_mode == Accurate ? m_options.videoEncoder : QString());
        if (error.isEmpty())
            error = exitCode == -1 ? tr("FFmpeg could not be started.")
                                   : tr("FFmpeg stopped with an error (code %1).").arg(exitCode);
        setErrorString(error, details);
        QFile::remove(m_output);
        setState(Failed);
    }
    emit finished();
}

void TrimJob::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void TrimJob::setProgress(double progress)
{
    if (qFuzzyCompare(progress, m_progress))
        return;
    m_progress = progress;
    emit progressChanged();
}

void TrimJob::setErrorString(const QString &error, const QString &details)
{
    if (error == m_errorString && details == m_errorDetails)
        return;
    m_errorString = error;
    m_errorDetails = details;
    emit errorStringChanged();
}

void TrimJob::fail(const QString &error)
{
    setErrorString(error);
    setState(Failed);
    emit finished();
}

void TrimJob::deleteOriginal()
{
    const QString input = m_source.toLocalFile();
    if (QFileInfo::exists(input) && !QFile::moveToTrash(input) && !QFile::remove(input)) {
        if (++m_deleteTries < kDeleteTries) {
            QTimer::singleShot(kDeleteRetryMs, this, &TrimJob::deleteOriginal);
            return;
        }
        return keepOriginal(tr("The original couldn't be deleted; another program may have it open."));
    }
    if (!m_finalOutput.isEmpty()) {
        if (!QFile::rename(m_output, m_finalOutput)) {
            setErrorString(tr("The original was moved to the trash, but the trimmed video couldn't be renamed. "
                              "It was saved as %1.").arg(QFileInfo(m_output).fileName()));
            setState(Failed);
            emit deleteOriginalFinished(m_source, QUrl::fromLocalFile(m_output), true);
            emit finished();
            return;
        }
        setOutput(m_finalOutput);
        m_finalOutput.clear();
    }
    setState(Finished);
    emit deleteOriginalFinished(m_source, QUrl::fromLocalFile(m_output), true);
    emit finished();
}

// The original couldn't be deleted: keep it, and give a trim that was meant
// to take its name an ordinary one.
void TrimJob::keepOriginal(const QString &error)
{
    if (!m_finalOutput.isEmpty()) {
        const QFileInfo info(m_finalOutput);
        QString copy;
        for (int n = 2; copy.isEmpty() || QFileInfo::exists(copy); ++n)
            copy = info.dir().filePath(QStringLiteral("%1_%2.%3").arg(info.completeBaseName()).arg(n).arg(info.suffix()));
        if (QFile::rename(m_output, copy))
            setOutput(copy);
        m_finalOutput.clear();
    }
    setErrorString(error + QLatin1Char(' ')
                   + tr("The trimmed video was saved as %1.").arg(QFileInfo(m_output).fileName()));
    setState(Failed);
    emit deleteOriginalFinished(m_source, m_source, false);
    emit finished();
}
