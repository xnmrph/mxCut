#include "Settings.h"

#include "Encoders.h"

#include <QFileInfo>

namespace {
const TrimOptions defaults;
const QString defaultInKey = QStringLiteral("A");
const QString defaultOutKey = QStringLiteral("D");
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    TrimJob::setFfmpegOverride(ffmpegPath());
}

QVariant Settings::value(const char *key, const QVariant &fallback) const
{
    return m_store.value(QLatin1String(key), fallback);
}

void Settings::store(const char *key, const QVariant &value)
{
    if (m_store.value(QLatin1String(key)) == value)
        return;
    m_store.setValue(QLatin1String(key), value);
    emit changed();
}

bool Settings::saveBesideSource() const { return value("output/besideSource", true).toBool(); }
void Settings::setSaveBesideSource(bool v) { store("output/besideSource", v); }

QUrl Settings::outputFolder() const { return value("output/folder", QUrl()).toUrl(); }
void Settings::setOutputFolder(const QUrl &v) { store("output/folder", v); }

QString Settings::filenamePattern() const
{
    return value("output/pattern", defaults.filenamePattern).toString();
}
void Settings::setFilenamePattern(const QString &v) { store("output/pattern", v); }

bool Settings::overwrite() const { return value("output/overwrite", defaults.overwrite).toBool(); }
void Settings::setOverwrite(bool v) { store("output/overwrite", v); }

// Was "Replace the original"; whoever turned that on keeps it on.
bool Settings::deleteOriginal() const
{
    return value("output/deleteOriginal", value("output/replaceOriginal", false)).toBool();
}
void Settings::setDeleteOriginal(bool v) { store("output/deleteOriginal", v); }

QString Settings::videoEncoder() const
{
    const QString id = value("encode/videoEncoder", defaults.videoEncoder).toString();
    return Encoders::find(id) ? id : defaults.videoEncoder;
}
void Settings::setVideoEncoder(const QString &v) { store("encode/videoEncoder", v); }

int Settings::quality() const { return qBound(0, value("encode/quality", defaults.quality).toInt(), 51); }
void Settings::setQuality(int v) { store("encode/quality", qBound(0, v, 51)); }

int Settings::speed() const { return qBound(0, value("encode/speed", defaults.speed).toInt(), 2); }
void Settings::setSpeed(int v) { store("encode/speed", qBound(0, v, 2)); }

bool Settings::copyAudio() const { return value("encode/copyAudio", defaults.copyAudio).toBool(); }
void Settings::setCopyAudio(bool v) { store("encode/copyAudio", v); }

int Settings::audioBitrate() const
{
    return qBound(32, value("encode/audioBitrate", defaults.audioBitrate).toInt(), 512);
}
void Settings::setAudioBitrate(int v) { store("encode/audioBitrate", qBound(32, v, 512)); }

int Settings::maxConcurrent() const { return qBound(1, value("queue/maxConcurrent", 2).toInt(), 8); }
void Settings::setMaxConcurrent(int v) { store("queue/maxConcurrent", qBound(1, v, 8)); }

QString Settings::ffmpegPath() const { return value("ffmpeg/path", QString()).toString(); }

void Settings::setFfmpegPath(const QString &v)
{
    if (v == ffmpegPath())
        return;
    store("ffmpeg/path", v);
    TrimJob::setFfmpegOverride(v);
    emit ffmpegPathChanged();
}

QString Settings::inKey() const { return value("keys/in", defaultInKey).toString(); }
void Settings::setInKey(const QString &v) { store("keys/in", v.isEmpty() ? defaultInKey : v); }

QString Settings::outKey() const { return value("keys/out", defaultOutKey).toString(); }
void Settings::setOutKey(const QString &v) { store("keys/out", v.isEmpty() ? defaultOutKey : v); }

TrimOptions Settings::trimOptions() const
{
    TrimOptions o;
    if (!saveBesideSource())
        o.outputFolder = outputFolder().toLocalFile();
    o.filenamePattern = filenamePattern();
    o.overwrite = overwrite();
    o.deleteOriginal = deleteOriginal();
    o.videoEncoder = videoEncoder();
    o.quality = quality();
    o.speed = speed();
    o.copyAudio = copyAudio();
    o.audioBitrate = audioBitrate();
    return o;
}

QString Settings::previewFileName(const QString &videoName, int mode) const
{
    TrimOptions o = trimOptions();
    o.deleteOriginal = false;
    o.outputFolder = QStringLiteral("/nonexistent-preview-folder");
    o.overwrite = true;
    return QFileInfo(TrimJob::outputPathFor(QStringLiteral("/") + videoName, TrimJob::Mode(mode), o, 83.5, 95.25))
        .fileName();
}

void Settings::resetEncoding()
{
    setVideoEncoder(defaults.videoEncoder);
    setQuality(defaults.quality);
    setSpeed(defaults.speed);
    setCopyAudio(defaults.copyAudio);
    setAudioBitrate(defaults.audioBitrate);
}

void Settings::resetShortcuts()
{
    setInKey(defaultInKey);
    setOutKey(defaultOutKey);
}
