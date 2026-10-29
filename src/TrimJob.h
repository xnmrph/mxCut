#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class Settings;

// How a trim is written. Settings supplies these; the defaults match a fresh
// install.
struct TrimOptions {
    QString outputFolder; // empty: beside the input
    QString filenamePattern = QStringLiteral("{name}_trim"); // see TrimJob::outputPathFor
    bool overwrite = false; // otherwise add _2, _3, ...
    // Once the trimmed video is written and checked, the original goes to the
    // trash, wherever the trim was saved. A trim may then take the original's
    // own name.
    bool deleteOriginal = false;
    // The rest apply to Accurate mode only.
    QString videoEncoder = QStringLiteral("libx264");
    int quality = 18;
    int speed = 0;
    bool copyAudio = false;
    int audioBitrate = 192; // kbit/s
};

// One FFmpeg trim of one input file. Used directly for single-video mode and,
// in folder mode, as an entry in the export queue.
class TrimJob : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)
    // A file name of the user's own, without the folder or extension, used
    // in place of the filename pattern. Empty: use the pattern.
    Q_PROPERTY(QString outputName READ outputName WRITE setOutputName NOTIFY outputNameChanged)
    Q_PROPERTY(double inPoint READ inPoint WRITE setInPoint NOTIFY inPointChanged)
    Q_PROPERTY(double outPoint READ outPoint WRITE setOutPoint NOTIFY outPointChanged)
    // Parts of the video to keep, as {inPoint, outPoint} objects. With two or
    // more they are joined, in time order, into one file; otherwise the in
    // and out points above are used.
    Q_PROPERTY(QVariantList segments READ segments WRITE setSegments NOTIFY segmentsChanged)
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    // FFmpeg's own last lines of output when a trim fails.
    Q_PROPERTY(QString errorDetails READ errorDetails NOTIFY errorStringChanged)

public:
    enum Mode {
        Fast,     // stream copy: lossless and quick, but cuts snap to keyframes
        Accurate, // re-encode: frame-exact cuts, slower
    };
    Q_ENUM(Mode)

    enum State { Idle, Running, Finished, Failed, Cancelled };
    Q_ENUM(State)

    explicit TrimJob(QObject *parent = nullptr);
    ~TrimJob() override;

    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    QString output() const { return m_output; }
    void setOutput(const QString &output);
    QString outputName() const { return m_outputName; }
    void setOutputName(const QString &name);
    double inPoint() const { return m_inPoint; }
    void setInPoint(double seconds);
    double outPoint() const { return m_outPoint; }
    void setOutPoint(double seconds);
    QVariantList segments() const;
    void setSegments(const QVariantList &segments);
    // Total length of what is kept.
    double keptDuration() const;
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    State state() const { return m_state; }
    bool running() const { return m_state == Running; }
    double progress() const { return m_progress; }
    QString errorString() const { return m_errorString; }
    QString errorDetails() const { return m_errorDetails; }
    TrimOptions options() const { return m_options; }
    void setOptions(const TrimOptions &options) { m_options = options; }

    // Takes the output and encoder options from Settings; call before start().
    Q_INVOKABLE void applySettings(Settings *settings);

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    // Back to Idle so a failed or cancelled job can run again.
    Q_INVOKABLE void reset();
    // Folder containing the output, for "Show in folder".
    Q_INVOKABLE QUrl outputFolderUrl() const;

    // The FFmpeg arguments for one in/out range with the current settings
    // (output must be set).
    QStringList arguments() const;

    // A path chosen in Settings wins, then FFmpeg next to the app binary (so
    // packaged builds use the bundled copy), then FFmpeg on PATH.
    static QString ffmpegPath();
    static void setFfmpegOverride(const QString &path);

    // The output file for a trim. The pattern's {name}, {in}, {out}, {mode},
    // {date} and {time} are filled in and the extension is added. Unless
    // overwriting, an existing file gets _2, _3, ... appended. The original's
    // own path is never used unless it is being deleted.
    static QString outputPathFor(const QString &input, Mode mode, const TrimOptions &options,
                                 double inPoint = 0, double outPoint = 0);
    // Turns FFmpeg's error output into a sentence a user can act on.
    static QString friendlyError(const QString &ffmpegOutput, const QString &encoder = {});

signals:
    void sourceChanged();
    void outputChanged();
    void outputNameChanged();
    void inPointChanged();
    void outPointChanged();
    void segmentsChanged();
    void modeChanged();
    void stateChanged();
    void progressChanged();
    void errorStringChanged();
    void finished();
    // Around deleting the original, so whoever has it open can let go of it
    // and then open what is there now: the trimmed video, or the original if
    // it couldn't be deleted.
    void aboutToDeleteOriginal(const QUrl &source);
    void deleteOriginalFinished(const QUrl &source, const QUrl &current, bool deleted);

private:
    void setState(State state);
    void setProgress(double progress);
    void setErrorString(const QString &error, const QString &details = {});
    void fail(const QString &error);
    struct Range {
        double in = 0;
        double out = 0;
    };
    QList<Range> ranges() const;
    QStringList argumentsFor(const Range &range, const QString &output) const;
    QStringList joinedAccurateArguments(const QList<Range> &ranges, bool withAudio) const;
    QStringList concatArguments(const QString &listFile) const;
    void runStep();
    void removeTemporaryFiles();
    void readProgress();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void deleteOriginal();
    void keepOriginal(const QString &error);

    QUrl m_source;
    QString m_output;
    QString m_outputName;
    double m_inPoint = 0;
    double m_outPoint = 0;
    QList<Range> m_segments;
    Mode m_mode = Fast;
    State m_state = Idle;
    double m_progress = 0;
    QString m_errorString;
    QString m_errorDetails;
    TrimOptions m_options;
    QPointer<QProcess> m_process;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrTail;
    int m_deleteTries = 0;
    // When the trim takes the original's own name, it is written to m_output
    // beside it and renamed to this once the original is gone.
    QString m_finalOutput;

    // A trim is one or more FFmpeg runs: one per kept part plus a join in
    // Fast mode, a single run otherwise.
    struct Step {
        QStringList arguments;
        double progressStart = 0;
        double progressPerSecond = 0;
    };
    QString m_ffmpeg;
    QList<Step> m_steps;
    qsizetype m_step = 0;
    QStringList m_temporaryFiles;
};
