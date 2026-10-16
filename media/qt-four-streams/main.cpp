// SPDX-License-Identifier: BSD-2-Clause
// Origin: EmberBSD contributors (AI-assisted).
#include <QApplication>
#include <QCommandLineParser>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPainter>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <sys/resource.h>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#if defined(__NetBSD__)
#include <link_elf.h>
#elif defined(__linux__)
#include <link.h>
#endif

namespace {
constexpr std::size_t streamCount = 4;

struct Fixture {
    int width = 0;
    int height = 0;
    int frames = 0;
    int fps = 0;
    bool enabled() const { return frames != 0; }
};

struct Configuration {
    std::array<QString, streamCount> paths;
    QString output;
    int timeoutMs = 0;
    int liveMs = 0;
    QSize windowSize{1280, 800};
    Fixture fixture;
    bool requireAll = false;
    bool qtConversion = false;
};

qint64 timevalUs(const timeval &value)
{
    return static_cast<qint64>(value.tv_sec) * 1000000
        + static_cast<qint64>(value.tv_usec);
}

double intervalFps(qint64 count, qint64 firstUs, qint64 lastUs)
{
    return count > 1 && lastUs > firstUs
        ? static_cast<double>(count - 1) * 1000000.0
            / static_cast<double>(lastUs - firstUs) : 0.0;
}

QString statusName(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::NoMedia: return QStringLiteral("NoMedia");
    case QMediaPlayer::LoadingMedia: return QStringLiteral("LoadingMedia");
    case QMediaPlayer::LoadedMedia: return QStringLiteral("LoadedMedia");
    case QMediaPlayer::StalledMedia: return QStringLiteral("StalledMedia");
    case QMediaPlayer::BufferingMedia: return QStringLiteral("BufferingMedia");
    case QMediaPlayer::BufferedMedia: return QStringLiteral("BufferedMedia");
    case QMediaPlayer::EndOfMedia: return QStringLiteral("EndOfMedia");
    case QMediaPlayer::InvalidMedia: return QStringLiteral("InvalidMedia");
    }
    return QStringLiteral("Unknown");
}

#if defined(__NetBSD__) || defined(__linux__)
// dl_iterate_phdr defines a non-const callback parameter.
int collectLibrary(dl_phdr_info *info, std::size_t, void *context)
{
    if (info->dlpi_name && *info->dlpi_name)
        static_cast<QStringList *>(context)->append(QString::fromUtf8(info->dlpi_name));
    return 0;
}
#endif

QStringList loadedLibraries()
{
    QStringList libraries;
#if defined(__NetBSD__) || defined(__linux__)
    dl_iterate_phdr(collectLibrary, &libraries);
#endif
    libraries.removeDuplicates();
    libraries.sort();
    return libraries;
}

struct AvFrameDeleter {
    void operator()(AVFrame *value) const { av_frame_free(&value); }
};
struct SwsDeleter {
    void operator()(SwsContext *value) const { sws_free_context(&value); }
};
struct FrameMailbox {
    std::atomic<bool> accepting{true};
    std::mutex mutex;
    std::deque<QVideoFrame> frames;
    qint64 notifications = 0, validSignals = 0, invalidSignals = 0;
    qint64 coalesced = 0, dequeued = 0, discardedOnStop = 0;
    std::size_t peak = 0;
    std::chrono::steady_clock::time_point firstSignal{}, lastSignal{};
};

struct Channel {
    QMediaPlayer *player = nullptr;
    QVideoSink *sink = nullptr;
    QImage image;
    QSize presentationSize;
    std::shared_ptr<FrameMailbox> mailbox = std::make_shared<FrameMailbox>();
    std::unique_ptr<SwsContext, SwsDeleter> scaler;
    std::unique_ptr<AVFrame, AvFrameDeleter> scalingInput;
    qint64 swscaleFrames = 0, qtImageFrames = 0;
    qint64 verificationUs = 0, conversionUs = 0;
    qint64 delivered = 0;
    qint64 painted = 0;
    qint64 paintedGeneration = 0;
    qint64 firstDeliveryUs = -1;
    qint64 lastDeliveryUs = -1;
    qint64 firstPaintUs = -1;
    qint64 lastPaintUs = -1;
    qint64 currentPts = -1;
    qint64 previousPts = -1;
    qint64 ptsEqual = 0;
    qint64 ptsRegressions = 0;
    qint64 ptsMissing = 0;
    qint64 invalidFrames = 0;
    qint64 formatChanges = 0;
    qint64 pixelChecks = 0;
    QSize sourceSize;
    QVideoFrameFormat::PixelFormat format = QVideoFrameFormat::Format_Invalid;
    QVideoFrame::HandleType handleType = QVideoFrame::NoHandle;
    int frameId = -1;
    int lastFrameId = -1;
    int playerError = 0;
    qint64 observationStartPts = -1;
    qint64 observationStartDelivered = 0;
    qint64 observationStartPainted = 0;
    int observationStartFrameId = -1;
    QString errorText;
    QMediaPlayer::MediaStatus status = QMediaPlayer::NoMedia;
    bool playing = false;
    bool eof = false;
    std::vector<bool> seen;
};

class FourStreams final : public QWidget {
public:
    explicit FourStreams(Configuration config)
        : config_(std::move(config))
    {
        clock_.start();
        baselineValid_ = getrusage(RUSAGE_SELF, &baseline_) == 0;
        resize(config_.windowSize);
        setWindowTitle(QStringLiteral("EmberBSD: four Qt Multimedia streams"));
        setAutoFillBackground(false);
        for (std::size_t index = 0; index < streamCount; ++index) {
            Channel &channel = channels_[index];
            channel.player = new QMediaPlayer(this);
            channel.sink = new QVideoSink(this);
            channel.player->setVideoSink(channel.sink);
            if (config_.fixture.enabled())
                channel.seen.resize(static_cast<std::size_t>(config_.fixture.frames), false);
            const auto mailbox = channel.mailbox;
            connect(channel.sink, &QVideoSink::videoFrameChanged, this,
                    [mailbox](const QVideoFrame &frame) {
                if (!mailbox->accepting.load())
                    return;
                const std::lock_guard<std::mutex> lock(mailbox->mutex);
                if (!mailbox->accepting.load())
                    return;
                ++mailbox->notifications;
                if (!frame.isValid()) {
                    ++mailbox->invalidSignals;
                    return;
                }
                const auto now = std::chrono::steady_clock::now();
                if (mailbox->validSignals++ == 0)
                    mailbox->firstSignal = now;
                mailbox->lastSignal = now;
                if (mailbox->frames.size() == 8) {
                    mailbox->frames.pop_front();
                    ++mailbox->coalesced;
                }
                mailbox->frames.push_back(frame);
                mailbox->peak = std::max(mailbox->peak, mailbox->frames.size());
            }, Qt::DirectConnection);
            connect(channel.player, &QMediaPlayer::playbackStateChanged, this,
                    [this, index](QMediaPlayer::PlaybackState state) {
                updateOverlap();
                channels_[index].playing = state == QMediaPlayer::PlayingState;
                updateOverlap();
            });
            connect(channel.player, &QMediaPlayer::errorOccurred, this,
                    [this, index](QMediaPlayer::Error error, const QString &message) {
                channels_[index].playerError = static_cast<int>(error);
                channels_[index].errorText = message;
                if (error != QMediaPlayer::NoError)
                    finish(3, QStringLiteral("Stream %1: %2")
                           .arg(static_cast<qulonglong>(index)).arg(message));
            });
            connect(channel.player, &QMediaPlayer::mediaStatusChanged, this,
                    [this, index](QMediaPlayer::MediaStatus status) {
                channels_[index].status = status;
                if (done_)
                    return;
                if (status == QMediaPlayer::InvalidMedia) {
                    finish(3, QStringLiteral("Stream %1: invalid media")
                           .arg(static_cast<qulonglong>(index)));
                } else if (status == QMediaPlayer::EndOfMedia) {
                    channels_[index].eof = true;
                    if (config_.liveMs != 0) {
                        finish(7, QStringLiteral("Stream %1 reached EOF before the live observation completed")
                               .arg(static_cast<qulonglong>(index)));
                        return;
                    }
                    scheduleEofCheck();
                }
            });
        }
        connect(&exposureTimer_, &QTimer::timeout, this, [this] { observeExposure(); });
        exposureTimer_.start(25);
        observationTimer_.setSingleShot(true);
        observationTimer_.setTimerType(Qt::PreciseTimer);
        connect(&observationTimer_, &QTimer::timeout, this, [this] { finishLiveObservation(); });
        framePump_.setTimerType(Qt::PreciseTimer);
        connect(&framePump_, &QTimer::timeout, this, [this] { pumpFrames(); });
        framePump_.start(10);
    }

    ~FourStreams() override
    {
        closeMailboxes();
        for (Channel &channel : channels_) {
            disconnect(channel.sink, nullptr, this, nullptr);
            channel.player->stop();
        }
    }

    void start()
    {
        if (!config_.output.isEmpty()) {
            QDir directory;
            if (QFileInfo::exists(config_.output) || !directory.mkpath(config_.output)) {
                finish(2, QStringLiteral("Acceptance output directory must be new"));
                return;
            }
            outputReady_ = true;
            csv_.setFileName(config_.output + QStringLiteral("/frames.csv"));
            if (!csv_.open(QIODevice::WriteOnly | QIODevice::Text)) {
                finish(2, QStringLiteral("Cannot create frames.csv: ") + csv_.errorString());
                return;
            }
            if (csv_.write("event,stream,delivery,frame_id,pts_us,end_us,wall_us,width,height,pixel_format,verified\n") < 0) {
                finish(2, QStringLiteral("Cannot write frames.csv header"));
                return;
            }
            const QString platform = QGuiApplication::platformName();
            if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal")
                    || platform == QStringLiteral("minimalegl")) {
                finish(2, QStringLiteral("Acceptance requires an exposed display window"));
                return;
            }
        }
        QStringList canonicalPaths;
        for (const QString &path : config_.paths) {
            const QFileInfo file(path);
            if (!file.isFile() || !file.isReadable() || file.canonicalFilePath().isEmpty()) {
                finish(2, QStringLiteral("Input must be a readable file: ") + path);
                return;
            }
            const QString canonical = file.canonicalFilePath();
            if (canonicalPaths.contains(canonical)) {
                finish(2, QStringLiteral("Four different input paths are required"));
                return;
            }
            canonicalPaths.append(canonical);
        }
        started_ = true;
        show();
        if (config_.timeoutMs != 0)
            QTimer::singleShot(config_.timeoutMs, this, [this] {
                finish(4, config_.liveMs == 0
                    ? QStringLiteral("Playback timeout before four streams reached EOF")
                    : QStringLiteral("Overall timeout before the live observation completed"));
            });
        QTimer::singleShot(0, this, [this] {
            for (std::size_t index = 0; index < streamCount; ++index) {
                if (done_)
                    return;
                channels_[index].player->setSource(QUrl::fromLocalFile(config_.paths[index]));
            }
            for (Channel &channel : channels_) {
                if (done_)
                    return;
                channel.player->play();
            }
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(15, 17, 20));
        const bool exposed = observeExposure();
        for (std::size_t index = 0; index < streamCount; ++index) {
            Channel &channel = channels_[index];
            const QRect tile = tileRect(index);
            painter.setPen(Qt::white);
            painter.drawText(tile.adjusted(8, 2, -8, -2), Qt::AlignLeft | Qt::AlignTop,
                             QStringLiteral("%1: %2  frames %3 / paints %4")
                             .arg(static_cast<qulonglong>(index))
                             .arg(QFileInfo(config_.paths[index]).fileName())
                             .arg(channel.delivered).arg(channel.painted));
            if (channel.image.isNull())
                continue;
            painter.drawImage(imageRect(index, channel.presentationSize), channel.image);
            if (exposed && !done_ && channel.paintedGeneration != channel.delivered) {
                const qint64 now = elapsedUs();
                channel.paintedGeneration = channel.delivered;
                ++channel.painted;
                if (channel.firstPaintUs < 0)
                    channel.firstPaintUs = now;
                channel.lastPaintUs = now;
                if (!writeCsv("paint", index, channel, channel.currentPts, -1, now,
                              config_.fixture.enabled()))
                    ioFailed_ = true;
            }
        }
    }

    void closeEvent(QCloseEvent *event) override
    {
        if (!done_)
            finish(5, QStringLiteral("Window closed before four streams reached EOF"));
        QWidget::closeEvent(event);
    }

private:
    qint64 elapsedUs() const { return clock_.nsecsElapsed() / 1000; }

    QRect tileRect(std::size_t index) const
    {
        return QRect(static_cast<int>(index % 2) * width() / 2,
                     static_cast<int>(index / 2) * height() / 2, width() / 2, height() / 2);
    }

    QRect imageRect(std::size_t index, QSize source) const
    {
        const QRect content = tileRect(index).adjusted(4, 28, -4, -4);
        const QSize fitted = source.scaled(content.size(), Qt::KeepAspectRatio);
        return QRect(content.center().x() - fitted.width() / 2,
                     content.center().y() - fitted.height() / 2, fitted.width(), fitted.height());
    }

    bool mailboxesEmpty() const
    {
        return std::all_of(channels_.cbegin(), channels_.cend(), [](const Channel &channel) {
            const std::lock_guard<std::mutex> lock(channel.mailbox->mutex);
            return channel.mailbox->frames.empty();
        });
    }

    void scheduleEofCheck()
    {
        if (done_ || config_.liveMs != 0 || eofCheckPending_ || !mailboxesEmpty()
                || !std::all_of(channels_.cbegin(), channels_.cend(),
                                [](const Channel &channel) { return channel.eof; }))
            return;
        eofCheckPending_ = true;
        QTimer::singleShot(150, this, [this] {
            eofCheckPending_ = false;
            if (!done_ && mailboxesEmpty())
                validateCompletion();
        });
    }

    void pumpFrames()
    {
        for (std::size_t index = 0; index < streamCount && !done_; ++index) {
            QVideoFrame frame;
            {
                FrameMailbox &mailbox = *channels_[index].mailbox;
                const std::lock_guard<std::mutex> lock(mailbox.mutex);
                if (mailbox.frames.empty())
                    continue;
                frame = std::move(mailbox.frames.front());
                mailbox.frames.pop_front();
                ++mailbox.dequeued;
            }
            receive(index, frame);
        }
        scheduleEofCheck();
    }

    void closeMailboxes()
    {
        framePump_.stop();
        for (Channel &channel : channels_) {
            channel.mailbox->accepting.store(false);
            const std::lock_guard<std::mutex> lock(channel.mailbox->mutex);
            channel.mailbox->discardedOnStop += static_cast<qint64>(channel.mailbox->frames.size());
            channel.mailbox->frames.clear();
        }
    }

    QImage displayImage(std::size_t index, QVideoFrame &frame, QString &error)
    {
        Channel &channel = channels_[index];
        const auto format = frame.surfaceFormat();
        const QSize target = imageRect(index, frame.size()).size();
        if (config_.qtConversion || frame.pixelFormat() != QVideoFrameFormat::Format_YUV420P
                || frame.handleType() != QVideoFrame::NoHandle || target.isEmpty()
                || format.colorSpace() != QVideoFrameFormat::ColorSpace_BT709
                || format.colorRange() != QVideoFrameFormat::ColorRange_Video
                || format.colorTransfer() != QVideoFrameFormat::ColorTransfer_BT709
                || format.scanLineDirection() != QVideoFrameFormat::TopToBottom
                || format.viewport() != QRect(QPoint(0, 0), frame.size())
                || format.rotation() != QtVideo::Rotation::None || format.isMirrored()
                || frame.rotation() != QtVideo::Rotation::None || frame.mirrored()) {
            QImage image = frame.toImage();
            if (!image.isNull())
                ++channel.qtImageFrames;
            channel.presentationSize = image.size();
            return image;
        }
        error = QStringLiteral("Cannot map, allocate or scale the display frame");
        if (!frame.map(QVideoFrame::ReadOnly))
            return {};
        const auto unmap = [](QVideoFrame *value) { value->unmap(); };
        const std::unique_ptr<QVideoFrame, decltype(unmap)> mapping(&frame, unmap);
        if (frame.planeCount() != 3 || frame.width() <= 0 || frame.height() <= 0)
            return {};
        if (!channel.scalingInput || channel.scalingInput->width != frame.width()
                || channel.scalingInput->height != frame.height()) {
            channel.scalingInput.reset(av_frame_alloc());
            if (!channel.scalingInput)
                return {};
            AVFrame &input = *channel.scalingInput;
            input.width = frame.width(); input.height = frame.height(); input.format = AV_PIX_FMT_YUV420P;
            if (av_frame_get_buffer(&input, 0) < 0) {
                channel.scalingInput.reset();
                return {};
            }
            std::memset(input.buf[0]->data, 0, input.buf[0]->size);
        }
        AVFrame &input = *channel.scalingInput;
        for (int plane = 0; plane < 3; ++plane) {
            const int columns = plane == 0 ? frame.width() : frame.width() / 2 + frame.width() % 2;
            const int rows = plane == 0 ? frame.height() : frame.height() / 2 + frame.height() % 2;
            const int stride = frame.bytesPerLine(plane);
            if (!frame.bits(plane) || stride < columns
                    || static_cast<qint64>(frame.mappedBytes(plane)) < static_cast<qint64>(stride) * (rows - 1) + columns)
                return {};
            for (int row = 0; row < rows; ++row)
                std::memcpy(input.data[plane] + static_cast<qint64>(row) * input.linesize[plane],
                            frame.bits(plane) + static_cast<qint64>(row) * stride, static_cast<std::size_t>(columns));
        }
        input.color_range = AVCOL_RANGE_MPEG; input.colorspace = AVCOL_SPC_BT709;
        input.color_primaries = AVCOL_PRI_BT709; input.color_trc = AVCOL_TRC_BT709;
        QImage image(target, QImage::Format_RGB32);
        std::unique_ptr<AVFrame, AvFrameDeleter> output(av_frame_alloc());
        if (image.isNull() || !output || image.bytesPerLine() > std::numeric_limits<int>::max())
            return {};
        output->width = target.width(); output->height = target.height(); output->format = AV_PIX_FMT_RGB32;
        output->data[0] = image.bits(); output->linesize[0] = static_cast<int>(image.bytesPerLine());
        output->color_range = AVCOL_RANGE_JPEG; output->colorspace = AVCOL_SPC_RGB;
        output->color_primaries = AVCOL_PRI_BT709; output->color_trc = AVCOL_TRC_BT709;
        if (!channel.scaler) {
            channel.scaler.reset(sws_alloc_context());
            if (!channel.scaler)
                return {};
            channel.scaler->scaler = SWS_SCALE_BILINEAR;
            channel.scaler->threads = 1;
        }
        const int status = sws_scale_frame(channel.scaler.get(), output.get(), &input);
        if (status < 0) {
            error = QStringLiteral("libswscale display conversion failed (%1)").arg(status);
            return {};
        }
        ++channel.swscaleFrames;
        channel.presentationSize = frame.size();
        return image;
    }

    bool observeExposure()
    {
        const bool exposed = isVisible() && windowHandle() && windowHandle()->isExposed();
        if (exposed && firstExposureUs_ < 0)
            firstExposureUs_ = elapsedUs();
        return exposed;
    }

    void updateOverlap()
    {
        const qint64 now = elapsedUs();
        const bool allPlaying = std::all_of(channels_.begin(), channels_.end(),
                                           [](const Channel &channel) { return channel.playing; });
        if (allPlaying && overlapStartUs_ < 0)
            overlapStartUs_ = now;
        if (!allPlaying && overlapStartUs_ >= 0) {
            overlapUs_ += now - overlapStartUs_;
            overlapStartUs_ = -1;
        }
    }

    bool writeCsv(const char *event, std::size_t index, const Channel &channel,
                  qint64 pts, qint64 end, qint64 wall, bool verified)
    {
        if (!csv_.isOpen())
            return true;
        const QByteArray row = QByteArray(event) + ',' + QByteArray::number(static_cast<qulonglong>(index))
            + ',' + QByteArray::number(channel.delivered) + ',' + QByteArray::number(channel.frameId)
            + ',' + QByteArray::number(pts) + ',' + QByteArray::number(end)
            + ',' + QByteArray::number(wall) + ',' + QByteArray::number(channel.sourceSize.width())
            + ',' + QByteArray::number(channel.sourceSize.height())
            + ',' + QByteArray::number(static_cast<int>(channel.format))
            + ',' + (verified ? "1\n" : "0\n");
        return csv_.write(row) == row.size();
    }

    bool verifyFixture(std::size_t index, QVideoFrame &frame, int &frameId, QString &error)
    {
        const Fixture &fixture = config_.fixture;
        if (frame.size() != QSize(fixture.width, fixture.height)
                || frame.pixelFormat() != QVideoFrameFormat::Format_YUV420P) {
            error = QStringLiteral("Fixture requires the configured dimensions and YUV420P");
            return false;
        }
        if (!frame.map(QVideoFrame::ReadOnly)) {
            error = QStringLiteral("Cannot map decoded fixture frame");
            return false;
        }
        bool valid = frame.planeCount() == 3;
        for (int plane = 0; valid && plane < 3; ++plane) {
            const int width = plane == 0 ? fixture.width : fixture.width / 2;
            const int height = plane == 0 ? fixture.height : fixture.height / 2;
            const int stride = frame.bytesPerLine(plane);
            valid = frame.bits(plane) && stride >= width
                && static_cast<qint64>(frame.mappedBytes(plane))
                    >= static_cast<qint64>(stride) * (height - 1) + width;
        }
        if (!valid) {
            frame.unmap();
            error = QStringLiteral("Decoded fixture plane bounds are invalid");
            return false;
        }
        const uchar *luma = frame.bits(0);
        frameId = 0;
        for (int bit = 0; bit < 8; ++bit) {
            const uchar value = luma[2 * bit];
            if (value != 16 && value != 235) {
                valid = false;
                break;
            }
            if (value == 235)
                frameId |= 1 << bit;
        }
        if (frameId >= fixture.frames)
            valid = false;
        std::vector<uchar> expected(static_cast<std::size_t>(fixture.width));
        for (int stripe = 0; valid && stripe * 64 < fixture.height; ++stripe) {
            for (int x = 0; x < fixture.width; x += 64) {
                const auto value = static_cast<uchar>(16 + ((x / 64 + 3 * stripe
                    + frameId * 5 + static_cast<int>(index) * 41) % 220));
                std::fill(expected.begin() + x, expected.begin() + std::min(x + 64, fixture.width), value);
            }
            const int firstRow = stripe * 64;
            const int lastRow = std::min(firstRow + 64, fixture.height);
            for (int y = firstRow; valid && y < lastRow; ++y) {
                if (y == 0 || y == 16) {
                    for (int x = 0; x < 16; ++x) {
                        expected[static_cast<std::size_t>(x)] = y == 0
                            ? static_cast<uchar>((frameId & (1 << (x / 2))) ? 235 : 16)
                            : static_cast<uchar>(16 + ((frameId * 5 + static_cast<int>(index) * 41) % 220));
                    }
                }
                const uchar *actual = luma + static_cast<qint64>(y) * frame.bytesPerLine(0);
                valid = std::memcmp(actual, expected.data(), expected.size()) == 0;
            }
        }
        std::fill(expected.begin(), expected.end(), static_cast<uchar>(128));
        for (int plane = 1; valid && plane < 3; ++plane) {
            for (int y = 0; valid && y < fixture.height / 2; ++y) {
                const uchar *actual = frame.bits(plane)
                    + static_cast<qint64>(y) * frame.bytesPerLine(plane);
                valid = std::memcmp(actual, expected.data(), static_cast<std::size_t>(fixture.width / 2)) == 0;
            }
        }
        frame.unmap();
        if (!valid)
            error = QStringLiteral("Decoded fixture pixels differ from the independent formula");
        return valid;
    }

    void receive(std::size_t index, const QVideoFrame &input)
    {
        if (done_)
            return;
        Channel &channel = channels_[index];
        if (!input.isValid()) {
            ++channel.invalidFrames;
            return;
        }
        const qint64 now = elapsedUs();
        if (ioFailed_) {
            finish(2, QStringLiteral("Cannot write per-frame CSV"));
            return;
        }
        QVideoFrame frame(input);
        int frameId = -1;
        if (config_.fixture.enabled()) {
            const qint64 verificationStart = elapsedUs();
            QString error;
            if (!verifyFixture(index, frame, frameId, error)) {
                finish(6, QStringLiteral("Stream %1: %2")
                       .arg(static_cast<qulonglong>(index)).arg(error));
                return;
            }
            if (frameId <= channel.lastFrameId || channel.seen[static_cast<std::size_t>(frameId)]) {
                finish(6, QStringLiteral("Fixture frame ID repeats or moves backwards"));
                return;
            }
            channel.seen[static_cast<std::size_t>(frameId)] = true;
            channel.lastFrameId = frameId;
            ++channel.pixelChecks;
            channel.verificationUs += elapsedUs() - verificationStart;
        }
        const qint64 pts = frame.startTime();
        if (pts < 0) {
            ++channel.ptsMissing;
        } else if (channel.previousPts >= 0) {
            if (pts < channel.previousPts)
                ++channel.ptsRegressions;
            else if (pts == channel.previousPts)
                ++channel.ptsEqual;
        }
        if (pts >= 0)
            channel.previousPts = pts;
        if (channel.delivered != 0
                && (channel.sourceSize != frame.size() || channel.format != frame.pixelFormat()))
            ++channel.formatChanges;
        const qint64 conversionStart = elapsedUs();
        QString conversionError;
        QImage image = displayImage(index, frame, conversionError);
        channel.conversionUs += elapsedUs() - conversionStart;
        if (image.isNull()) {
            finish(6, conversionError.isEmpty() ? QStringLiteral("QVideoFrame::toImage returned an empty image") : conversionError);
            return;
        }
        channel.image = std::move(image);
        channel.sourceSize = frame.size();
        channel.format = frame.pixelFormat();
        channel.handleType = frame.handleType();
        channel.frameId = frameId;
        channel.currentPts = pts;
        ++channel.delivered;
        if (channel.firstDeliveryUs < 0)
            channel.firstDeliveryUs = now;
        channel.lastDeliveryUs = now;
        if (!writeCsv("frame", index, channel, pts, frame.endTime(), now, config_.fixture.enabled())) {
            finish(2, QStringLiteral("Cannot write per-frame CSV"));
            return;
        }
        update();
        startLiveObservation();
    }

    void startLiveObservation()
    {
        if (config_.liveMs == 0 || observationStartUs_ >= 0 || done_)
            return;
        if (!std::all_of(channels_.begin(), channels_.end(),
                         [](const Channel &channel) { return channel.delivered > 0; }))
            return;
        observationStartUs_ = elapsedUs();
        for (Channel &channel : channels_) {
            channel.observationStartPts = channel.currentPts;
            channel.observationStartFrameId = channel.frameId;
            channel.observationStartDelivered = channel.delivered;
            channel.observationStartPainted = channel.painted;
        }
        observationTimer_.start(config_.liveMs);
    }

    void finishLiveObservation()
    {
        if (done_)
            return;
        const qint64 requestedUs = static_cast<qint64>(config_.liveMs) * 1000;
        const qint64 elapsed = elapsedUs() - observationStartUs_;
        if (elapsed < requestedUs) {
            observationTimer_.start(static_cast<int>((requestedUs - elapsed + 999) / 1000));
            return;
        }
        observationEndUs_ = elapsedUs();
        const bool inactive = std::any_of(channels_.cbegin(), channels_.cend(), [](const Channel &channel) {
            return !channel.playing || channel.eof
                || channel.delivered <= channel.observationStartDelivered
                || channel.painted <= channel.observationStartPainted;
        });
        if (inactive) {
            finish(7, QStringLiteral("Each live stream must continue delivering and painting during the observation"));
            return;
        }
        validateCompletion();
    }

    void validateCompletion()
    {
        if (done_)
            return;
        if (firstExposureUs_ < 0 || !observeExposure()) {
            finish(7, QStringLiteral("The window was not exposed on a display"));
            return;
        }
        if (ioFailed_) {
            finish(2, QStringLiteral("Cannot write per-frame CSV"));
            return;
        }
        qint64 first = 0;
        qint64 last = std::numeric_limits<qint64>::max();
        for (const Channel &channel : channels_) {
            if (channel.delivered < 2 || channel.painted == 0 || channel.image.isNull()) {
                finish(7, QStringLiteral("Each quadrant must display real frames"));
                return;
            }
            if (channel.ptsRegressions != 0 || channel.ptsMissing != 0) {
                finish(7, QStringLiteral("A stream has missing or regressing frame timestamps"));
                return;
            }
            if (config_.requireAll
                    && std::count(channel.seen.begin(), channel.seen.end(), true) != config_.fixture.frames) {
                finish(7, QStringLiteral("Not all expected fixture frames reached QVideoSink"));
                return;
            }
            first = std::max(first, channel.firstDeliveryUs);
            last = std::min(last, channel.lastDeliveryUs);
        }
        updateOverlap();
        if (overlapUs_ <= 0 && overlapStartUs_ < 0) {
            finish(7, QStringLiteral("The four players did not overlap in PlayingState"));
            return;
        }
        if (last <= first) {
            finish(7, QStringLiteral("The four actual frame-delivery intervals did not overlap"));
            return;
        }
        finish(0, config_.liveMs == 0
            ? QStringLiteral("Four real streams reached EOF and displayed overlapping decoded frames")
            : QStringLiteral("Four real streams displayed overlapping decoded frames during the timed live observation"));
    }

    QJsonObject report(int code, const QString &message, qint64 wallUs,
                       const rusage &usage, bool usageValid) const
    {
        QJsonObject result;
        result.insert(QStringLiteral("schema"), QStringLiteral("emberbsd.qt-four-streams.v1"));
        result.insert(QStringLiteral("pass"), code == 0);
        result.insert(QStringLiteral("exit_code"), code);
        result.insert(QStringLiteral("message"), message);
        result.insert(QStringLiteral("started"), started_);
        result.insert(QStringLiteral("requested_completion"), config_.liveMs == 0
                      ? QStringLiteral("all-eof") : QStringLiteral("live-observation"));
        result.insert(QStringLiteral("completion"), code != 0 ? QStringLiteral("failed")
                      : config_.liveMs == 0 ? QStringLiteral("all-eof") : QStringLiteral("live-observation"));
        result.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));
        result.insert(QStringLiteral("qpa_platform"), QGuiApplication::platformName());
        result.insert(QStringLiteral("media_backend_environment"), qEnvironmentVariable("QT_MEDIA_BACKEND"));
        result.insert(QStringLiteral("ffmpeg_decoding_hw_device_types_environment"),
                      qEnvironmentVariable("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"));
        result.insert(QStringLiteral("loaded_libraries"), QJsonArray::fromStringList(loadedLibraries()));
        result.insert(QStringLiteral("qt_library_paths"), QJsonArray::fromStringList(QCoreApplication::libraryPaths()));
        result.insert(QStringLiteral("first_exposure_us"), firstExposureUs_);
        result.insert(QStringLiteral("window_width"), width());
        result.insert(QStringLiteral("window_height"), height());
        result.insert(QStringLiteral("wall_us"), wallUs);
        result.insert(QStringLiteral("live_observation_requested_ms"), config_.liveMs);
        result.insert(QStringLiteral("live_observation_start_wall_us"), observationStartUs_);
        result.insert(QStringLiteral("live_observation_end_wall_us"), observationEndUs_);
        result.insert(QStringLiteral("live_observation_duration_us"), observationStartUs_ >= 0
                      && observationEndUs_ >= observationStartUs_
                      ? observationEndUs_ - observationStartUs_ : 0);
        result.insert(QStringLiteral("four_players_playing_overlap_us"), overlapUs_);
        result.insert(QStringLiteral("full_pixel_verification"), config_.fixture.enabled());
        result.insert(QStringLiteral("force_qt_conversion"), config_.qtConversion);
        result.insert(QStringLiteral("libswscale_version"), static_cast<qint64>(swscale_version()));
        result.insert(QStringLiteral("frame_mailbox_capacity_per_stream"), 8);
        result.insert(QStringLiteral("require_all_fixture_frames"), config_.requireAll);
        result.insert(QStringLiteral("fixture_nominal_fps"), config_.fixture.fps);
        result.insert(QStringLiteral("fps_definition"), QStringLiteral("(observed_count-1)/(last_observation-first_observation), wall seconds"));
        result.insert(QStringLiteral("paint_definition"), QStringLiteral("A new delivered image drawn by QPainter while the window is exposed"));
        result.insert(QStringLiteral("cpu_usage_valid"), usageValid);
        if (usageValid) {
            const qint64 userUs = timevalUs(usage.ru_utime) - timevalUs(baseline_.ru_utime);
            const qint64 systemUs = timevalUs(usage.ru_stime) - timevalUs(baseline_.ru_stime);
            result.insert(QStringLiteral("cpu_user_us"), userUs);
            result.insert(QStringLiteral("cpu_system_us"), systemUs);
            result.insert(QStringLiteral("cpu_percent_one_core_100"), wallUs > 0
                ? static_cast<double>(userUs + systemUs) * 100.0 / static_cast<double>(wallUs) : 0.0);
            result.insert(QStringLiteral("maxrss_raw"), static_cast<qint64>(usage.ru_maxrss));
#if defined(__APPLE__)
            result.insert(QStringLiteral("maxrss_raw_unit"), QStringLiteral("bytes"));
            result.insert(QStringLiteral("maxrss_bytes"), static_cast<qint64>(usage.ru_maxrss));
#else
            result.insert(QStringLiteral("maxrss_raw_unit"), QStringLiteral("KiB"));
            result.insert(QStringLiteral("maxrss_bytes"), static_cast<qint64>(usage.ru_maxrss) * 1024);
#endif
        }
        QJsonArray streams;
        qint64 commonFirst = 0;
        qint64 commonLast = std::numeric_limits<qint64>::max();
        for (std::size_t index = 0; index < streamCount; ++index) {
            const Channel &channel = channels_[index];
            QJsonObject item;
            item.insert(QStringLiteral("stream"), static_cast<qint64>(index));
            item.insert(QStringLiteral("path"), config_.paths[index]);
            item.insert(QStringLiteral("source_url"), channel.player->source().toString());
            item.insert(QStringLiteral("eof"), channel.eof);
            item.insert(QStringLiteral("media_status"), statusName(channel.status));
            item.insert(QStringLiteral("player_error"), channel.playerError);
            item.insert(QStringLiteral("player_error_text"), channel.errorText);
            item.insert(QStringLiteral("video_codec_metadata"), channel.player->metaData()
                        .stringValue(QMediaMetaData::VideoCodec));
            item.insert(QStringLiteral("video_frame_rate_metadata"), channel.player->metaData()
                        .value(QMediaMetaData::VideoFrameRate).toDouble());
            item.insert(QStringLiteral("width"), channel.sourceSize.width());
            item.insert(QStringLiteral("height"), channel.sourceSize.height());
            item.insert(QStringLiteral("pixel_format"), static_cast<int>(channel.format));
            item.insert(QStringLiteral("frame_handle_type"), static_cast<int>(channel.handleType));
            item.insert(QStringLiteral("libswscale_viewport_frames"), channel.swscaleFrames);
            item.insert(QStringLiteral("qt_to_image_frames"), channel.qtImageFrames);
            item.insert(QStringLiteral("display_conversion"), channel.swscaleFrames > 0
                        ? (channel.qtImageFrames > 0 ? QStringLiteral("mixed")
                           : QStringLiteral("libswscale-bilinear-bt709-video-yuv420p-to-viewport-rgb32"))
                        : (channel.qtImageFrames > 0 ? QStringLiteral("qt-to-image") : QStringLiteral("none")));
            item.insert(QStringLiteral("display_image_width"), channel.image.width());
            item.insert(QStringLiteral("display_image_height"), channel.image.height());
            item.insert(QStringLiteral("verification_wall_us"), channel.verificationUs);
            item.insert(QStringLiteral("conversion_wall_us"), channel.conversionUs);
            {
                const FrameMailbox &mailbox = *channel.mailbox;
                const std::lock_guard<std::mutex> lock(channel.mailbox->mutex);
                const auto span = std::chrono::duration_cast<std::chrono::microseconds>(mailbox.lastSignal - mailbox.firstSignal).count();
                item.insert(QStringLiteral("sink_frame_notifications"), mailbox.notifications);
                item.insert(QStringLiteral("sink_valid_frame_notifications"), mailbox.validSignals);
                item.insert(QStringLiteral("sink_invalid_frame_notifications"), mailbox.invalidSignals);
                item.insert(QStringLiteral("sink_signal_span_us"), static_cast<qint64>(span));
                item.insert(QStringLiteral("mailbox_dequeued_frames"), mailbox.dequeued);
                item.insert(QStringLiteral("mailbox_coalesced_frames"), mailbox.coalesced);
                item.insert(QStringLiteral("mailbox_discarded_on_stop"), mailbox.discardedOnStop);
                item.insert(QStringLiteral("mailbox_peak_frames"), static_cast<qint64>(mailbox.peak));
            }
            item.insert(QStringLiteral("delivered_frames"), channel.delivered);
            item.insert(QStringLiteral("painted_distinct_frames"), channel.painted);
            item.insert(QStringLiteral("delivered_not_painted_frames"), channel.delivered - channel.painted);
            item.insert(QStringLiteral("invalid_frame_notifications"), channel.invalidFrames + channel.mailbox->invalidSignals);
            item.insert(QStringLiteral("format_changes"), channel.formatChanges);
            item.insert(QStringLiteral("first_delivery_us"), channel.firstDeliveryUs);
            item.insert(QStringLiteral("last_delivery_us"), channel.lastDeliveryUs);
            item.insert(QStringLiteral("first_paint_us"), channel.firstPaintUs);
            item.insert(QStringLiteral("last_paint_us"), channel.lastPaintUs);
            item.insert(QStringLiteral("observed_delivery_fps"), intervalFps(channel.delivered,
                        channel.firstDeliveryUs, channel.lastDeliveryUs));
            item.insert(QStringLiteral("observed_paint_fps"), intervalFps(channel.painted,
                        channel.firstPaintUs, channel.lastPaintUs));
            item.insert(QStringLiteral("equal_pts"), channel.ptsEqual);
            item.insert(QStringLiteral("regressing_pts"), channel.ptsRegressions);
            item.insert(QStringLiteral("missing_pts"), channel.ptsMissing);
            item.insert(QStringLiteral("fixture_verified_frames"), channel.pixelChecks);
            if (observationStartUs_ >= 0) {
                item.insert(QStringLiteral("observation_start_source_pts_us"), channel.observationStartPts);
                item.insert(QStringLiteral("observation_end_source_pts_us"), channel.currentPts);
                item.insert(QStringLiteral("observation_start_frame_id"), channel.observationStartFrameId);
                item.insert(QStringLiteral("observation_end_frame_id"), channel.frameId);
                item.insert(QStringLiteral("observation_delivered_frames"),
                            channel.delivered - channel.observationStartDelivered);
                item.insert(QStringLiteral("observation_painted_distinct_frames"),
                            channel.painted - channel.observationStartPainted);
            }
            if (config_.fixture.enabled()) {
                QJsonArray missing;
                for (int frame = 0; frame < config_.fixture.frames; ++frame) {
                    if (!channel.seen[static_cast<std::size_t>(frame)])
                        missing.append(frame);
                }
                item.insert(QStringLiteral("fixture_expected_frames"), config_.fixture.frames);
                item.insert(QStringLiteral("fixture_missing_frame_ids"), missing);
                item.insert(QStringLiteral("fixture_missing_frames"), static_cast<qint64>(missing.size()));
            }
            commonFirst = std::max(commonFirst, channel.firstDeliveryUs);
            commonLast = std::min(commonLast, channel.lastDeliveryUs);
            streams.append(item);
        }
        result.insert(QStringLiteral("four_delivery_intervals_overlap_us"),
                      std::max<qint64>(0, commonLast - commonFirst));
        result.insert(QStringLiteral("streams"), streams);
        return result;
    }

    void finish(int code, QString message)
    {
        if (done_)
            return;
        done_ = true;
        closeMailboxes();
        const qint64 wallUs = elapsedUs();
        if (observationStartUs_ >= 0 && observationEndUs_ < 0)
            observationEndUs_ = wallUs;
        if (overlapStartUs_ >= 0) {
            overlapUs_ += wallUs - overlapStartUs_;
            overlapStartUs_ = -1;
        }
        exposureTimer_.stop();
        observationTimer_.stop();
        if (csv_.isOpen()) {
            if (!csv_.flush() || ioFailed_) {
                code = 2;
                message = QStringLiteral("Cannot flush per-frame CSV");
            }
            csv_.close();
        }
        rusage usage{};
        const bool usageValid = getrusage(RUSAGE_SELF, &usage) == 0 && baselineValid_;
        if (!usageValid && code == 0) {
            code = 2;
            message = QStringLiteral("getrusage failed");
        }
        // Snapshot the exposed QWidget before stopping players. grab() may repaint,
        // but done_ prevents that diagnostic repaint from increasing paint counts.
        if (outputReady_ && firstExposureUs_ >= 0
                && !grab().save(config_.output + QStringLiteral("/window.png"))) {
            code = 2;
            message = QStringLiteral("Cannot save the displayed window image");
        }
        QByteArray json = QJsonDocument(report(code, message, wallUs, usage, usageValid))
            .toJson(QJsonDocument::Indented);
        if (outputReady_) {
            QSaveFile output(config_.output + QStringLiteral("/report.json"));
            if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size() || !output.commit()) {
                code = 2;
                message = QStringLiteral("Cannot save report.json");
                json = QJsonDocument(report(code, message, wallUs, usage, usageValid))
                    .toJson(QJsonDocument::Indented);
                std::fprintf(stderr, "Cannot save report.json\n");
            }
        }
        const auto bytes = static_cast<std::size_t>(json.size());
        if (std::fwrite(json.constData(), 1, bytes, stdout) != bytes)
            code = 2;
        std::fflush(stdout);
        for (Channel &channel : channels_) {
            disconnect(channel.sink, nullptr, this, nullptr);
            channel.player->stop();
        }
        QCoreApplication::exit(code);
    }

    Configuration config_;
    std::array<Channel, streamCount> channels_;
    QElapsedTimer clock_;
    QTimer exposureTimer_;
    QTimer observationTimer_;
    QTimer framePump_;
    QFile csv_;
    rusage baseline_{};
    qint64 firstExposureUs_ = -1;
    qint64 overlapStartUs_ = -1;
    qint64 overlapUs_ = 0;
    qint64 observationStartUs_ = -1;
    qint64 observationEndUs_ = -1;
    bool baselineValid_ = false;
    bool started_ = false;
    bool done_ = false;
    bool ioFailed_ = false;
    bool outputReady_ = false;
    bool eofCheckPending_ = false;
};

bool parseSize(const QString &text, int &width, int &height)
{
    const QRegularExpression expression(QStringLiteral("^([1-9][0-9]*)x([1-9][0-9]*)$"));
    const QRegularExpressionMatch match = expression.match(text);
    bool widthOk = false;
    bool heightOk = false;
    if (match.hasMatch()) {
        width = match.captured(1).toInt(&widthOk);
        height = match.captured(2).toInt(&heightOk);
    }
    return widthOk && heightOk && width <= 32768 && height <= 32768;
}

bool parseFixture(const QString &text, Fixture &fixture)
{
    const QStringList fields = text.split(QLatin1Char(':'));
    bool framesOk = false;
    bool fpsOk = false;
    if (fields.size() != 3 || !parseSize(fields[0], fixture.width, fixture.height))
        return false;
    fixture.frames = fields[1].toInt(&framesOk);
    fixture.fps = fields[2].toInt(&fpsOk);
    return framesOk && fpsOk && fixture.width >= 16 && fixture.height >= 16
        && fixture.width % 2 == 0 && fixture.height % 2 == 0
        && fixture.frames >= 2 && fixture.frames <= 256 && fixture.fps >= 1 && fixture.fps <= 240;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qt-four-streams"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Display four actual Qt Multimedia players in a raster QWidget."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption accept(QStringList{QStringLiteral("accept")},
        QStringLiteral("Write finite acceptance evidence to a new directory."), QStringLiteral("directory"));
    const QCommandLineOption timeout(QStringList{QStringLiteral("timeout-ms")},
        QStringLiteral("Fail after this wall timeout. Acceptance default: 60000."), QStringLiteral("milliseconds"));
    const QCommandLineOption live(QStringList{QStringLiteral("live-ms")},
        QStringLiteral("Accept a timed live observation after all four first frames, instead of waiting for EOF."),
        QStringLiteral("milliseconds"));
    const QCommandLineOption size(QStringList{QStringLiteral("window-size")},
        QStringLiteral("Display window size. Default: 1280x800."), QStringLiteral("WIDTHxHEIGHT"));
    const QCommandLineOption fixture(QStringList{QStringLiteral("verify-fixtures")},
        QStringLiteral("Verify every delivered I420 fixture pixel and recover frame IDs."),
        QStringLiteral("WIDTHxHEIGHT:FRAMES:FPS"));
    const QCommandLineOption requireAll(QStringList{QStringLiteral("require-all-frames")},
        QStringLiteral("Fail if any expected fixture frame does not reach QVideoSink."));
    const QCommandLineOption qtConversion(QStringList{QStringLiteral("qt-conversion")},
        QStringLiteral("Use Qt toImage for every frame instead of viewport-sized libswscale conversion."));
    parser.addOptions({accept, timeout, live, size, fixture, requireAll, qtConversion});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Four distinct readable media files."),
                                 QStringLiteral("FILE0 FILE1 FILE2 FILE3"));
    parser.process(app);
    const QStringList paths = parser.positionalArguments();
    if (paths.size() != static_cast<qsizetype>(streamCount))
        parser.showHelp(2);
    Configuration config;
    config.qtConversion = parser.isSet(qtConversion);
    for (std::size_t index = 0; index < streamCount; ++index)
        config.paths[index] = QFileInfo(paths[static_cast<qsizetype>(index)]).absoluteFilePath();
    if (parser.isSet(accept)) {
        if (parser.value(accept).isEmpty())
            parser.showHelp(2);
        config.output = QFileInfo(parser.value(accept)).absoluteFilePath();
        config.timeoutMs = 60000;
    }
    if (parser.isSet(timeout)) {
        bool ok = false;
        config.timeoutMs = parser.value(timeout).toInt(&ok);
        if (!ok || config.timeoutMs < 1)
            parser.showHelp(2);
    }
    if (parser.isSet(size)) {
        int width = 0;
        int height = 0;
        if (!parseSize(parser.value(size), width, height) || width < 160 || height < 120)
            parser.showHelp(2);
        config.windowSize = QSize(width, height);
    }
    if (parser.isSet(fixture) && !parseFixture(parser.value(fixture), config.fixture))
        parser.showHelp(2);
    config.requireAll = parser.isSet(requireAll);
    if (config.requireAll && !config.fixture.enabled())
        parser.showHelp(2);
    if (parser.isSet(live)) {
        bool ok = false;
        config.liveMs = parser.value(live).toInt(&ok);
        if (!ok || config.liveMs < 1 || config.output.isEmpty()
                || config.timeoutMs <= config.liveMs || config.requireAll)
            parser.showHelp(2);
    }
    FourStreams window(std::move(config));
    QTimer::singleShot(0, &window, [&window] { window.start(); });
    return app.exec();
}
