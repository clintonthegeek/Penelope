#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include <ctime>
#include <atomic>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QStringList>
#include <QTabWidget>
#include <QTextStream>
#include <QUrl>

#include <KActionCollection>
#include <KLocalizedString>

#if defined(__linux__) && defined(__x86_64__)
#include <csignal>
#include <sys/time.h>
#include <ucontext.h>
#include <dlfcn.h>
#endif

#include <poppler-qt6.h>

#include "contentbuilder.h"
#include "documenttab.h"
#include "fontmanager.h"
#include "layoutengine.h"
#include "mainwindow.h"
#include "pdfgenerator.h"
#include "penelopesettings.h"
#include "textshaper.h"


// Opt-in sampling profiler. Wall-clock timing on a loaded machine cannot
// resolve single-digit effects, and hand-placed timers kept reporting zero
// because of scope mistakes; sampling the instruction pointer and symbolising
// afterwards provides leaf-PC clues, not call stacks or stage attribution.
// Linux x86-64 only. Keep profiling runs separate from timing runs.
#if defined(__linux__) && defined(__x86_64__)
namespace prof {
static const int kMaxSamples = 500000;
static std::atomic<quintptr> g_pcs[kMaxSamples]{};
static std::atomic<int> g_count{0};
static std::atomic<bool> g_enabled{false};
static_assert(std::atomic<quintptr>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

static void handler(int, siginfo_t *, void *uc)
{
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    const int index = g_count.fetch_add(1, std::memory_order_relaxed);
    if (index >= kMaxSamples) return;
    ucontext_t *c = static_cast<ucontext_t *>(uc);
    g_pcs[index].store(quintptr(c->uc_mcontext.gregs[REG_RIP]), std::memory_order_relaxed);
}

void start()
{
    g_enabled.store(true, std::memory_order_relaxed);
    struct sigaction sa{};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, nullptr) != 0) qFatal("Cannot install profile signal handler");
    struct itimerval tv;
    tv.it_interval.tv_sec = 0;
    tv.it_interval.tv_usec = 1000;
    tv.it_value = tv.it_interval;
    if (setitimer(ITIMER_PROF, &tv, nullptr) != 0) qFatal("Cannot start profile timer");
}

void stopAndDump()
{
    g_enabled.store(false, std::memory_order_relaxed);
    struct itimerval off;
    off.it_interval.tv_sec = 0;
    off.it_interval.tv_usec = 0;
    off.it_value = off.it_interval;
    setitimer(ITIMER_PROF, &off, nullptr);

    const QString directory = qEnvironmentVariable("BENCH_PROFILE_DIR",
                                                  QDir::tempPath() + QStringLiteral("/penelope-profile-%1").arg(QCoreApplication::applicationPid()));
    if (!QDir().mkpath(directory)) {
        qWarning("Cannot create profile output directory");
        return;
    }
    const QString maps = directory + "/maps.txt";
    const QString out = directory + "/samples.txt";
    QFile m("/proc/self/maps");
    if (m.open(QIODevice::ReadOnly)) {
        QFile o(maps);
        if (o.open(QIODevice::WriteOnly))
            o.write(m.readAll());
    }
    void *base = nullptr;
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly))
        return;
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&g_pcs), &info) && info.dli_fbase) {
        base = info.dli_fbase;
    }
    QHash<quintptr, int> hist;
    const int count = std::min(g_count.load(std::memory_order_relaxed), kMaxSamples);
    for (int i = 0; i < count; ++i) {
        const quintptr pc = g_pcs[i].load(std::memory_order_relaxed);
        if (pc) hist[pc]++;
    }
    for (auto it = hist.cbegin(); it != hist.cend(); ++it) {
        f.write(QStringLiteral("%1 %2\n")
                    .arg(it.key() - quintptr(base), 0, 16)
                    .arg(it.value()).toUtf8());
    }
    QTextStream(stderr) << "Profile samples: " << directory << Qt::endl;
}
} // namespace prof
#endif

namespace {

// A PDF is not byte-reproducible: /CreationDate and the trailer /ID are both
// derived from the current clock. Mask them so the remaining bytes can be
// compared across builds when checking that an emission change is inert.
void maskFrom(QByteArray &buf, const QByteArray &marker, const QByteArray &replacement,
              char terminator)
{
    int at = 0;
    while ((at = buf.indexOf(marker, at)) >= 0) {
        const int start = at + marker.size() - 1;
        int end = start;
        while (end < buf.size() && buf.at(end) != terminator)
            ++end;
        buf.replace(start, end - start, replacement);
        at = start + replacement.size();
    }
}

QByteArray normalizePdf(const QByteArray &pdf)
{
    QByteArray out = pdf;
    maskFrom(out, QByteArray("/CreationDate D:"), QByteArray("NORMALIZED"), 'Z');
    maskFrom(out, QByteArray("/ID ["), QByteArray("NORMALIZED]"), ']');
    return out;
}

// Process CPU time excludes time spent waiting to be scheduled. It still
// responds to clock frequency, memory contention and other threads in this
// process; retain wall time and individual samples alongside it.
class StageTimer {
public:
    void start() { m_cpuStart = std::clock(); m_wall.start(); }
    qint64 nsecsElapsed() const { return m_wall.nsecsElapsed(); }
    qint64 cpuNsecsElapsed() const {
        return qint64(double(std::clock() - m_cpuStart) * 1e9 / CLOCKS_PER_SEC);
    }
private:
    QElapsedTimer m_wall;
    std::clock_t m_cpuStart = 0;
};

struct StageTimes {
    qint64 parseNs = 0;
    qint64 layoutNs = 0;
    qint64 continuousNs = 0;
    qint64 pdfNs = 0;
    qint64 rasterNs = 0;
    qint64 parseCpuNs = 0;
    qint64 layoutCpuNs = 0;
    qint64 continuousCpuNs = 0;
    qint64 pdfCpuNs = 0;
    qint64 rasterCpuNs = 0;
    int pages = 0;
    int blocks = 0;
    qint64 pdfBytes = 0;
    qint64 openCpuNs() const { return parseCpuNs + layoutCpuNs + pdfCpuNs; }
    qint64 pipelineNs() const { return parseNs + layoutNs + pdfNs; }
};

struct Stats {
    qint64 median = 0;
    qint64 min = 0;
};

Stats summarize(QList<qint64> samples)
{
    Stats s;
    if (samples.isEmpty())
        return s;
    std::sort(samples.begin(), samples.end());
    s.min = samples.first();
    s.median = samples[samples.size() / 2];
    return s;
}

QString ms(qint64 ns) { return QString::number(double(ns) / 1e6, 'f', 1); }

StageTimes measureOnce(const QString &markdown, bool hyphenate, bool measureContinuous,
                       bool measureRaster, qreal zoom, int compressLevel, int runTextMode,
                       FontManager &fonts, TextShaper &shaper)
{
    StageTimes t;
    StageTimer timer;

    ContentBuilder builder;
    builder.setFootnoteStyle(FootnoteStyle());
    timer.start();
    Content::Document doc = builder.build(markdown);
    t.parseNs = timer.nsecsElapsed();
    t.parseCpuNs = timer.cpuNsecsElapsed();
    t.blocks = int(doc.blocks.size());

    Layout::Engine engine(&fonts, &shaper);
    engine.setHyphenateJustifiedText(hyphenate);
    PageLayout pl;

    fonts.resetUsage();
    timer.start();
    Layout::LayoutResult paginated = engine.layout(doc, pl);
    t.layoutNs = timer.nsecsElapsed();
    t.layoutCpuNs = timer.cpuNsecsElapsed();
    t.pages = int(paginated.pages.size());

    // No resetUsage() here: the shaper marks glyphs during layout and
    // generate() embeds the subset from that set, exactly as the reader does.
    PdfGenerator generator(&fonts);
    generator.setCompressionLevel(compressLevel);
    if (runTextMode >= 0)
        generator.setRunTextOperators(runTextMode == 1);
    timer.start();
    QByteArray pdf = generator.generate(paginated, pl, QStringLiteral("bench"));
    t.pdfNs = timer.nsecsElapsed();
    t.pdfCpuNs = timer.cpuNsecsElapsed();
    t.pdfBytes = pdf.size();
    if (pdf.isEmpty()) qFatal("PDF generation failed");

    if (measureContinuous) {
        fonts.resetUsage();
        timer.start();
        Layout::ContinuousLayoutResult continuous =
            engine.layoutContinuous(doc, pl.contentSizePoints().width());
        t.continuousNs = timer.nsecsElapsed();
        t.continuousCpuNs = timer.cpuNsecsElapsed();
        Q_UNUSED(continuous);
    }

    if (measureRaster && !pdf.isEmpty()) {
        timer.start();
        auto poppler = Poppler::Document::loadFromData(pdf);
        if (poppler) {
            poppler->setRenderHint(Poppler::Document::Antialiasing, true);
            poppler->setRenderHint(Poppler::Document::TextAntialiasing, true);
            const qreal dpr = 1.0;
            const int n = std::min(3, poppler->numPages());
            for (int i = 0; i < n; ++i) {
                std::unique_ptr<Poppler::Page> page(poppler->page(i));
                if (!page)
                    continue;
                const QSizeF pageSize = page->pageSizeF();
                const int w = qRound(pageSize.width() * zoom);
                const int h = qRound(pageSize.height() * zoom);
                const qreal xres = 72.0 * w / pageSize.width() * dpr;
                const qreal yres = 72.0 * h / pageSize.height() * dpr;
                page->renderToImage(xres, yres, -1, -1, w * dpr, h * dpr);
            }
        }
        t.rasterNs = timer.nsecsElapsed();
        t.rasterCpuNs = timer.cpuNsecsElapsed();
    }

    return t;
}

QString syntheticDoc(int paragraphs)
{
    static const char *kLorem[] = {
        "The renderer shapes text with HarfBuzz and breaks lines with ICU, so "
        "measurement dominates cost for prose-heavy documents.",
        "Layout assigns blocks to pages, repeats table headers across page breaks "
        "and keeps source coordinates for every fragment it emits.",
        "Continuous layout shares loaded fonts with the paginated path but "
        "produces a single tall column instead of discrete pages.",
        "Font subsetting marks glyphs as they are used, which means a second full "
        "pass over the same document is cheaper than the first.",
    };
    QString md;
    md += QStringLiteral("# Synthetic benchmark document\n\n");
    for (int i = 0; i < paragraphs; ++i) {
        if (i % 25 == 0)
            md += QStringLiteral("## Section %1\n\n").arg(i / 25 + 1);
        if (i % 7 == 0) {
            md += QStringLiteral("- item one for section %1\n- item two with `inline code`\n"
                                 "- item three referencing [a link](https://example.org)\n\n")
                      .arg(i);
        } else if (i % 11 == 0) {
            md += QStringLiteral("| Column A | Column B |\n| --- | --- |\n");
            for (int r = 0; r < 3; ++r)
                md += QStringLiteral("| cell %1 | value %2 |\n").arg(r).arg(i);
            md += QLatin1Char('\n');
        } else {
            md += QString::fromLatin1(kLorem[i % 4]);
            md += QLatin1Char(' ');
            md += QString::fromLatin1(kLorem[(i + 1) % 4]);
            md += QStringLiteral(" (paragraph %1).\n\n").arg(i);
        }
    }
    return md;
}

struct Row {
    QString label;
    qint64 bytes = 0;
    Stats parse, layout, continuous, pdf, raster, open;
    Stats parseCpu, layoutCpu, pdfCpu, openCpu;
    QList<StageTimes> samples;
    int pages = 0;
    int blocks = 0;
};

Row runRow(const QString &label, const QString &markdown, int iterations, bool hyphenate,
           bool measureContinuous, bool measureRaster, qreal zoom, int compressLevel, int runTextMode,
           bool warmFonts)
{
    FontManager fonts;
    TextShaper shaper(&fonts);
    auto measure = [&]() {
        if (warmFonts)
            return measureOnce(markdown, hyphenate, measureContinuous, measureRaster,
                               zoom, compressLevel, runTextMode, fonts, shaper);
        FontManager coldFonts;
        TextShaper coldShaper(&coldFonts);
        return measureOnce(markdown, hyphenate, measureContinuous, measureRaster,
                           zoom, compressLevel, runTextMode, coldFonts, coldShaper);
    };
    measure();

    QList<qint64> parse, layout, continuous, pdf, raster, open;
    QList<qint64> parseCpu, layoutCpu, pdfCpu, openCpu;
    Row row;
    row.label = label;
    row.bytes = markdown.toUtf8().size();

    for (int i = 0; i < iterations; ++i) {
        StageTimes t = measure();
        row.samples.append(t);
        parseCpu.append(t.parseCpuNs);
        layoutCpu.append(t.layoutCpuNs);
        pdfCpu.append(t.pdfCpuNs);
        openCpu.append(t.openCpuNs());
        parse.append(t.parseNs);
        layout.append(t.layoutNs);
        continuous.append(t.continuousNs);
        pdf.append(t.pdfNs);
        raster.append(t.rasterNs);
        open.append(t.pipelineNs());
        row.pages = t.pages;
        row.blocks = t.blocks;
    }

    row.parseCpu = summarize(parseCpu);
    row.layoutCpu = summarize(layoutCpu);
    row.pdfCpu = summarize(pdfCpu);
    row.openCpu = summarize(openCpu);
    row.parse = summarize(parse);
    row.layout = summarize(layout);
    row.continuous = summarize(continuous);
    row.pdf = summarize(pdf);
    row.raster = summarize(raster);
    row.open = summarize(open);
    return row;
}

struct OpenResult {
    qint64 openCallNs = 0;
    qint64 renderReadyNs = 0;
    qint64 busyNs = 0;
    qint64 maxBlockNs = 0;
    int blockCount = 0;
    int turns = 0;
    int tabs = 0;
};

OpenResult measureOpen(const QString &path, bool webMode, qint64 windowNs)
{
    OpenResult r;

    PenelopeSettings::self()->setUseWebView(webMode);
    PenelopeSettings::self()->setAutoReloadOnChange(false);

    MainWindow window;
    window.resize(1000, 800);
    window.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);

    QElapsedTimer timer;
    timer.start();

    QObject::connect(&window, &MainWindow::documentRendered, &window, [&r, &timer](DocumentTab *) {
        if (!r.renderReadyNs) r.renderReadyNs = timer.nsecsElapsed();
    });
    QElapsedTimer callTimer;
    callTimer.start();
    window.openFile(QUrl::fromLocalFile(path));
    r.openCallNs = callTimer.nsecsElapsed();

    while (timer.nsecsElapsed() < windowNs) {
        const qint64 before = timer.nsecsElapsed();
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 gap = timer.nsecsElapsed() - before;

        QThread::msleep(1);
        ++r.turns;
        if (gap > 1'000'000)
            r.busyNs += gap;
        if (gap > 20'000'000) {
            r.maxBlockNs = std::max(r.maxBlockNs, gap);
            ++r.blockCount;
        }
    }

    if (auto *tabs = window.findChild<QTabWidget *>())
        r.tabs = tabs->count();
    return r;
}

struct RebuildResult {
    qint64 toFirstBusyNs = 0;
    qint64 renderReadyNs = 0;
    qint64 toLastBusyNs = 0;
    qint64 maxBlockNs = 0;
    int blockCount = 0;
};

// Heuristic event-loop activity after an edit, including the debounce.
// These timings do not identify paint or render completion events.
RebuildResult measureRebuild(const QString &path, bool webMode, qint64 windowNs)
{
    RebuildResult r;

    PenelopeSettings::self()->setUseWebView(webMode);
    PenelopeSettings::self()->setAutoReloadOnChange(false);

    MainWindow window;
    window.resize(1000, 800);
    window.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    window.openFile(QUrl::fromLocalFile(path));

    QElapsedTimer settle;
    settle.start();
    while (settle.nsecsElapsed() < 1'500'000'000LL) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }

    auto *tab = window.findChild<DocumentTab *>();
    if (!tab)
        return r;

    if (auto *action = window.actionCollection()->action(QStringLiteral("view_source_mode")))
        action->trigger();
    QApplication::processEvents(QEventLoop::AllEvents, 20);

    QElapsedTimer timer;
    timer.start();
    QObject::connect(&window, &MainWindow::documentRendered, &window, [&r, &timer](DocumentTab *) {
        if (!r.renderReadyNs) r.renderReadyNs = timer.nsecsElapsed();
    });
    tab->sourceEditor()->insertPlainText(QStringLiteral("x"));
    if (auto *action = window.actionCollection()->action(webMode ? QStringLiteral("view_web_mode")
                                                              : QStringLiteral("view_print_mode")))
        action->trigger();

    qint64 lastBusy = 0;
    bool painted = false;
    while (timer.nsecsElapsed() < windowNs) {
        const qint64 before = timer.nsecsElapsed();
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 gap = timer.nsecsElapsed() - before;

        if (gap > 1'000'000) {
            lastBusy = timer.nsecsElapsed();
            if (!painted) {
                r.toFirstBusyNs = timer.nsecsElapsed();
                painted = true;
            }
            if (gap > 20'000'000) {
                r.maxBlockNs = std::max(r.maxBlockNs, gap);
                ++r.blockCount;
            }
        }
        QThread::msleep(1);
    }
    r.toLastBusyNs = lastBusy > 0 ? lastBusy : timer.nsecsElapsed();
    return r;
}

QString pad(const QString &s, int width)
{
    if (s.size() >= width)
        return s.left(width);
    return s + QString(width - s.size(), QLatin1Char(' '));
}

void printRow(const Row &r, bool showContinuous, bool showRaster)
{
    QTextStream out(stdout);
    QStringList cols;
    cols << pad(r.label, 24) << pad(QString::number(r.bytes), 9) << pad(QString::number(r.blocks), 8)
         << pad(QString::number(r.pages), 7) << pad(ms(r.parse.median), 9)
         << pad(ms(r.layout.median), 9) << pad(ms(r.pdf.median), 9);
    if (showContinuous)
        cols << pad(r.continuous.median ? ms(r.continuous.median) : QStringLiteral("-"), 9);
    if (showRaster)
        cols << pad(r.raster.median ? ms(r.raster.median) : QStringLiteral("-"), 9);
    cols << pad(ms(r.open.median), 9)
         << pad(ms(r.parseCpu.median), 9) << pad(ms(r.layoutCpu.median), 9)
         << pad(ms(r.pdfCpu.median), 9) << pad(ms(r.openCpu.median), 9);
    out << cols.join(QLatin1Char('\t')) << Qt::endl;
}

void printHeader(bool showContinuous, bool showRaster)
{
    QTextStream out(stdout);
    QStringList cols;
    cols << pad(QStringLiteral("document"), 24) << pad(QStringLiteral("bytes"), 9)
         << pad(QStringLiteral("blocks"), 8) << pad(QStringLiteral("pages"), 7)
         << pad(QStringLiteral("parse"), 9) << pad(QStringLiteral("layout"), 9)
         << pad(QStringLiteral("pdf"), 9);
    if (showContinuous)
        cols << pad(QStringLiteral("cont"), 9);
    if (showRaster)
        cols << pad(QStringLiteral("raster3"), 9);
    cols << pad(QStringLiteral("pipeline"), 9)
         << pad(QStringLiteral("parseCPU"), 9) << pad(QStringLiteral("layoutCPU"), 9)
         << pad(QStringLiteral("pdfCPU"), 9) << pad(QStringLiteral("pipeCPU"), 9);
    out << cols.join(QLatin1Char('\t')) << Qt::endl;
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir config;
    if (!config.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_DATA_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("penelope_renderbench"));
    KLocalizedString::setApplicationDomain("penelope");

    QStringList args = QApplication::arguments();
    QString file;
    QString jsonPath;
    bool warmFonts = false;
    QList<int> sizes = {25, 100, 400, 1600, 6400};
    int iterations = 5;
    bool continuous = false;
    bool raster = false;
    bool hyphenate = false;
    bool openMode = false;
    bool rebuildMode = false;
    bool hashMode = false;
    qreal zoom = 1.0;
    int compressLevel = -1;
    int runTextMode = -1;
    qint64 windowMs = 3000;

    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QStringLiteral("--warm-fonts")) {
            warmFonts = true;
        } else if (a == QStringLiteral("--json") && i + 1 < args.size()) {
            jsonPath = args.at(++i);
        } else if (a == QStringLiteral("--file") && i + 1 < args.size()) {
            file = args.at(++i);
        } else if (a == QStringLiteral("--sizes") && i + 1 < args.size()) {
            sizes.clear();
            const QStringList parts = args.at(++i).split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &p : parts)
                sizes.append(p.trimmed().toInt());
        } else if (a == QStringLiteral("--iterations") && i + 1 < args.size()) {
            iterations = args.at(++i).toInt();
        } else if (a == QStringLiteral("--continuous")) {
            continuous = true;
        } else if (a == QStringLiteral("--raster")) {
            raster = true;
        } else if (a == QStringLiteral("--hyphenate")) {
            hyphenate = true;
        } else if (a == QStringLiteral("--open")) {
            openMode = true;
        } else if (a == QStringLiteral("--rebuild")) {
            rebuildMode = true;
        } else if (a == QStringLiteral("--hash")) {
            hashMode = true;
        } else if (a == QStringLiteral("--window-ms") && i + 1 < args.size()) {
            windowMs = args.at(++i).toLongLong();
        } else if (a == QStringLiteral("--zoom") && i + 1 < args.size()) {
            zoom = args.at(++i).toDouble();
        } else if (a == QStringLiteral("--compress-level") && i + 1 < args.size()) {
            compressLevel = args.at(++i).toInt();
        } else if (a == QStringLiteral("--run-text")) {
            runTextMode = 1;
        } else if (a == QStringLiteral("--no-run-text")) {
            runTextMode = 0;
        }
    }
    if (iterations < 1)
        iterations = 1;

    if (rebuildMode) {
        if (file.isEmpty()) {
            QTextStream(stderr) << QStringLiteral("--rebuild requires --file\n") << Qt::endl;
            return 1;
        }
        QTextStream out(stdout);
        out << QStringLiteral("mode=rebuild  edit then return to reader; ready=snapshot installed, busy=event-loop heuristic\n")
            << Qt::endl;
        out << pad(QStringLiteral("view"), 8) << pad(QStringLiteral("ready"), 10) << pad(QStringLiteral("firstBusy"), 11)
            << pad(QStringLiteral("lastBusy"), 10) << pad(QStringLiteral("maxStall"), 10)
            << pad(QStringLiteral("stalls"), 8) << Qt::endl;
        for (bool web : {false, true}) {
            RebuildResult r = measureRebuild(file, web, windowMs * 1'000'000LL);
            out << pad(web ? QStringLiteral("web") : QStringLiteral("page"), 8)
                << pad(r.renderReadyNs ? ms(r.renderReadyNs) : QStringLiteral("timeout"), 10)
                << pad(ms(r.toFirstBusyNs), 11) << pad(ms(r.toLastBusyNs), 10)
                << pad(ms(r.maxBlockNs), 10) << pad(QString::number(r.blockCount), 8)
                << Qt::endl;
        }
        return 0;
    }

    if (hashMode) {
        QTextStream out(stdout);
        QStringList sources;
        if (!file.isEmpty()) {
            sources << file;
        } else {
            for (int n : std::as_const(sizes))
                sources << QStringLiteral("synthetic:%1").arg(n);
        }
        for (const QString &src : std::as_const(sources)) {
            QString md;
            QString label = src;
            if (src.startsWith(QLatin1String("synthetic:"))) {
                md = syntheticDoc(src.mid(10).toInt());
            } else {
                QFile f(src);
                if (!f.open(QIODevice::ReadOnly)) {
                    QTextStream(stderr) << QStringLiteral("cannot read %1\n").arg(src) << Qt::endl;
                    return 1;
                }
                md = QString::fromUtf8(f.readAll());
                label = QFileInfo(src).fileName();
            }
            FontManager fonts;
            ContentBuilder builder;
            builder.setFootnoteStyle(FootnoteStyle());
            Content::Document doc = builder.build(md);
            TextShaper shaper(&fonts);
            Layout::Engine engine(&fonts, &shaper);
            PageLayout pl;
            fonts.resetUsage();
            Layout::LayoutResult laid = engine.layout(doc, pl);
            PdfGenerator gen(&fonts);
            gen.setCompressionLevel(compressLevel);
            if (runTextMode >= 0)
                gen.setRunTextOperators(runTextMode == 1);
            const QByteArray pdf = gen.generate(laid, pl, QStringLiteral("bench"));
            const QByteArray digest =
                QCryptographicHash::hash(normalizePdf(pdf), QCryptographicHash::Sha256).toHex();
            out << pad(label, 28) << pad(QString::number(pdf.size()), 10)
                << QString::fromLatin1(digest) << Qt::endl;
            const QString dumpTag = qEnvironmentVariable("BENCH_DUMP_PDF");
            if (!dumpTag.isEmpty() && src == QLatin1String("synthetic:100")) {
                QFile outFile(QStringLiteral("/tmp/opencode/dump-%1.pdf").arg(dumpTag));
                if (!outFile.open(QIODevice::WriteOnly) || outFile.write(pdf) != pdf.size())
                    return 1;
            }
        }
        return 0;
    }

    if (openMode) {
        if (file.isEmpty()) {
            QTextStream(stderr) << QStringLiteral("--open requires --file\n") << Qt::endl;
            return 1;
        }
        QTextStream out(stdout);
        out << QStringLiteral("mode=open  window=%1ms  openCall=sync openFile cost, ready=snapshot installed (not first raster), busy=post-open event-loop work, stalls>20ms\n")
                   .arg(windowMs)
            << Qt::endl;
        out << pad(QStringLiteral("view"), 8) << pad(QStringLiteral("ready"), 10) << pad(QStringLiteral("openCall"), 10)
            << pad(QStringLiteral("busy"), 10) << pad(QStringLiteral("total"), 10)
            << pad(QStringLiteral("maxStall"), 10) << pad(QStringLiteral("stalls"), 8)
            << pad(QStringLiteral("tabs"), 6) << Qt::endl;

        for (bool web : {false, true}) {
            OpenResult r = measureOpen(file, web, windowMs * 1'000'000LL);
            out << pad(web ? QStringLiteral("web") : QStringLiteral("page"), 8)
                << pad(r.renderReadyNs ? ms(r.renderReadyNs) : QStringLiteral("timeout"), 10)
                << pad(ms(r.openCallNs), 10) << pad(ms(r.busyNs), 10)
                << pad(ms(r.openCallNs + r.busyNs), 10) << pad(ms(r.maxBlockNs), 10)
                << pad(QString::number(r.blockCount), 8) << pad(QString::number(r.tabs), 6)
                << Qt::endl;
        }
        return 0;
    }

#if defined(__linux__) && defined(__x86_64__)
    const bool profiling = qEnvironmentVariableIsSet("BENCH_PROFILE");
    if (profiling)
        prof::start();
#endif
    QTextStream out(stdout);
    out << QStringLiteral("penelope render benchmark  (iterations=%1, hyphenate=%2, warm-fonts=%3; times in ms)\n")
               .arg(iterations)
               .arg(hyphenate ? QStringLiteral("on") : QStringLiteral("off"))
               .arg(warmFonts ? QStringLiteral("on") : QStringLiteral("off"));
    out << Qt::endl;

    QList<Row> rows;

    if (!file.isEmpty()) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << QStringLiteral("cannot read %1\n").arg(file) << Qt::endl;
            return 1;
        }
        const QString md = QString::fromUtf8(f.readAll());
        rows.append(runRow(QFileInfo(file).fileName(), md, iterations, hyphenate, continuous,
                           raster, zoom, compressLevel, runTextMode, warmFonts));
    } else {
        for (int n : std::as_const(sizes)) {
            const QString md = syntheticDoc(n);
            rows.append(runRow(QStringLiteral("synthetic-%1p").arg(n), md, iterations, hyphenate,
                               continuous, raster, zoom, compressLevel, runTextMode, warmFonts));
        }
    }

    printHeader(continuous, raster);
    for (const Row &r : std::as_const(rows))
        printRow(r, continuous, raster);

    if (!jsonPath.isEmpty()) {
        QJsonArray documents;
        for (const auto &row : rows) {
            QJsonArray samples;
            for (const auto &t : row.samples) {
                samples.append(QJsonObject{
                    {"parse_wall_ns", t.parseNs}, {"parse_cpu_ns", t.parseCpuNs},
                    {"layout_wall_ns", t.layoutNs}, {"layout_cpu_ns", t.layoutCpuNs},
                    {"continuous_wall_ns", t.continuousNs}, {"continuous_cpu_ns", t.continuousCpuNs},
                    {"pdf_wall_ns", t.pdfNs}, {"pdf_cpu_ns", t.pdfCpuNs}, {"pdf_bytes", t.pdfBytes},
                    {"raster_wall_ns", t.rasterNs}, {"raster_cpu_ns", t.rasterCpuNs}
                });
            }
            documents.append(QJsonObject{{"label", row.label}, {"bytes", row.bytes},
                                         {"pages", row.pages}, {"blocks", row.blocks},
                                         {"samples", samples}});
        }
        QFile output(jsonPath);
        const auto data = QJsonDocument(QJsonObject{
            {"warm_fonts", warmFonts}, {"compression_level", compressLevel},
            {"run_text", runTextMode != 0}, {"hyphenate", hyphenate},
            {"continuous", continuous}, {"raster", raster},
            {"documents", documents}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) {
            QTextStream(stderr) << "Cannot write benchmark JSON: " << jsonPath << Qt::endl;
            return 1;
        }
    }

#if defined(__linux__) && defined(__x86_64__)
    if (profiling)
        prof::stopAndDump();
#endif
    return 0;
}