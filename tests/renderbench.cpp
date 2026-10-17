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
#include <algorithm>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QLatin1Char>
#include <QPainter>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QStringList>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
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

#include "compositioncontroller.h"
#include "contentbuilder.h"
#include "documenttab.h"
#include "fontmanager.h"
#include "hyphenator.h"
#include "layoutengine.h"
#include "mainwindow.h"
#include "pdfgenerator.h"
#include "pdfpageitem.h"
#include "penelopesettings.h"
#include "rendercache.h"
#include "textshaper.h"
#include "webviewitem.h"


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

QString codeDoc(int lines)
{
    QString md = QStringLiteral("```cpp\n");
    for (int i = 0; i < lines; ++i) {
        md += QStringLiteral("int value_%1 = (%1 * 17) % 13; // measured line of a fenced block\n").arg(i);
    }
    md += QStringLiteral("```\n");
    return md;
}

QString wallDoc(int repeats)
{
    QString md = QStringLiteral("A single paragraph keeps growing so the reader can see one element hold every line. ");
    for (int i = 0; i < repeats; ++i) {
        md += QStringLiteral("Sentence %1 repeats the same prose so line breaking, not Markdown structure, dominates. ")
                  .arg(i);
    }
    md += QLatin1Char('\n');
    return md;
}

QJsonObject hostSnapshot()
{
    QJsonObject host;
    host.insert(QStringLiteral("logical_cpus"), QThread::idealThreadCount());
    QFile load(QStringLiteral("/proc/loadavg"));
    if (load.open(QIODevice::ReadOnly))
        host.insert(QStringLiteral("loadavg"), QString::fromUtf8(load.readAll()).trimmed());
    QFile pressure(QStringLiteral("/proc/pressure/cpu"));
    if (pressure.open(QIODevice::ReadOnly))
        host.insert(QStringLiteral("cpu_pressure"), QString::fromUtf8(pressure.readAll()).trimmed());
    return host;
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    if (path.isEmpty())
        return true;
    QFile output(path);
    const QByteArray data = QJsonDocument(object).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()) {
        QTextStream(stderr) << QStringLiteral("Cannot write benchmark JSON: %1\n").arg(path);
        return false;
    }
    return true;
}

bool inlinesHaveSoftHyphen(const QList<Content::InlineNode> &nodes)
{
    for (const auto &node : nodes) {
        const bool found = std::visit([](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Content::TextRun> || std::is_same_v<T, Content::InlineCode>
                          || std::is_same_v<T, Content::Link>)
                return value.text.contains(QChar(0x00ad));
            else
                return false;
        }, node);
        if (found)
            return true;
    }
    return false;
}

bool blocksHaveSoftHyphen(const QList<Content::BlockNode> &blocks)
{
    for (const auto &block : blocks) {
        const bool found = std::visit([](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Content::Paragraph> || std::is_same_v<T, Content::Heading>)
                return inlinesHaveSoftHyphen(value.inlines);
            else if constexpr (std::is_same_v<T, Content::CodeBlock>)
                return value.code.contains(QChar(0x00ad));
            else if constexpr (std::is_same_v<T, Content::BlockQuote>)
                return blocksHaveSoftHyphen(value.children);
            else if constexpr (std::is_same_v<T, Content::List>) {
                for (const auto &item : value.items) {
                    if (blocksHaveSoftHyphen(item.children))
                        return true;
                }
                return false;
            } else if constexpr (std::is_same_v<T, Content::Table>) {
                for (const auto &row : value.rows) {
                    for (const auto &cell : row.cells) {
                        if (inlinesHaveSoftHyphen(cell.inlines))
                            return true;
                    }
                }
                return false;
            } else {
                return false;
            }
        }, block);
        if (found)
            return true;
    }
    return false;
}

bool documentHasSoftHyphen(const Content::Document &document)
{
    return blocksHaveSoftHyphen(document.blocks);
}

int linesInElement(const Layout::PageElement &element, int *maxLines, qreal *maxHeight)
{
    int count = 0;
    qreal height = 0;
    std::visit([&](const auto &box) {
        using T = std::decay_t<decltype(box)>;
        height = box.height;
        if constexpr (std::is_same_v<T, Layout::BlockBox>) {
            count = box.lines.size();
        } else if constexpr (std::is_same_v<T, Layout::TableBox>) {
            for (const auto &row : box.rows) {
                for (const auto &cell : row.cells)
                    count += cell.lines.size();
            }
        } else if constexpr (std::is_same_v<T, Layout::FootnoteSectionBox>) {
            for (const auto &note : box.footnotes)
                count += note.lines.size();
        }
    }, element);
    if (maxLines)
        *maxLines = std::max(*maxLines, count);
    if (maxHeight)
        *maxHeight = std::max(*maxHeight, height);
    return count;
}

void summarizeElements(const QList<Layout::PageElement> &elements, int *lines, int *maxLines, qreal *maxHeight)
{
    int total = 0;
    int widest = 0;
    qreal tallest = 0;
    for (const auto &element : elements)
        total += linesInElement(element, &widest, &tallest);
    if (lines)
        *lines = total;
    if (maxLines)
        *maxLines = widest;
    if (maxHeight)
        *maxHeight = tallest;
}

// Direct item paint. A real QGraphicsView translates into item coordinates and
// clips to the viewport. `unculled` instead passes the whole item rectangle as
// exposedRect, which is what Qt does when ItemUsesExtendedStyleOption is missing.
qint64 paintWeb(WebViewItem *item, const QRectF &viewport, bool unculled, qint64 *cpuNs)
{
    const int width = std::max(1, qCeil(viewport.width()));
    const int height = std::max(1, qCeil(viewport.height()));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    if (!unculled)
        painter.translate(-viewport.left(), -viewport.top());
    painter.setClipRect(viewport);
    QStyleOptionGraphicsItem option;
    option.exposedRect = unculled ? item->boundingRect() : viewport;
    option.rect = option.exposedRect.toAlignedRect();
    StageTimer timer;
    timer.start();
    item->paint(&painter, &option, nullptr);
    if (cpuNs)
        *cpuNs = timer.cpuNsecsElapsed();
    return timer.nsecsElapsed();
}

struct CompSample {
    bool ok = false;
    bool timeout = false;
    QString error;
    qint64 readyNs = 0;
    qint64 parseNs = 0;
    qint64 layoutNs = 0;
    qint64 pdfNs = 0;
    bool reused = false;
    quint64 hits = 0;
    quint64 misses = 0;
    int pages = 0;
    int elements = 0;
    int lines = 0;
    int maxLines = 0;
    qreal maxElementHeight = 0;
    qreal height = 0;
    qint64 pdfBytes = 0;
    bool softHyphens = false;
    CompositionResultPtr result;
};

CompSample waitForComposition(CompositionController &controller, CompositionRequest request, int timeoutMs)
{
    CompSample sample;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    const quint64 id = controller.submit(std::move(request));
    QObject::connect(&controller, &CompositionController::ready, &loop,
                     [&](quint64 readyId, CompositionResultPtr result) {
        if (readyId != id)
            return;
        sample.ok = true;
        sample.result = std::move(result);
        loop.quit();
    });
    QObject::connect(&controller, &CompositionController::failed, &loop,
                     [&](quint64 failedId, const QString &error) {
        if (failedId != id)
            return;
        sample.error = error;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    StageTimer clock;
    clock.start();
    timer.start(timeoutMs);
    loop.exec();
    sample.readyNs = clock.nsecsElapsed();
    if (!sample.ok && sample.error.isEmpty()) {
        sample.timeout = true;
        sample.error = QStringLiteral("timeout");
        return sample;
    }
    if (!sample.ok)
        return sample;
    const auto &result = *sample.result;
    sample.parseNs = result.parseNs;
    sample.layoutNs = result.layoutNs;
    sample.pdfNs = result.pdfNs;
    sample.softHyphens = documentHasSoftHyphen(result.document);
    sample.reused = result.reusedContent;
    sample.hits = result.shapeHits;
    sample.misses = result.shapeMisses;
    sample.pdfBytes = result.pdf.size();
    if (result.request.continuous) {
        sample.elements = result.continuousLayout.elements.size();
        sample.height = result.continuousLayout.totalHeight;
        summarizeElements(result.continuousLayout.elements, &sample.lines, &sample.maxLines,
                          &sample.maxElementHeight);
        sample.ok = sample.elements > 0;
    } else {
        sample.pages = result.paginated.pages.size();
        for (const auto &page : result.paginated.pages) {
            sample.elements += page.elements.size();
            int pageLines = 0;
            int pageMax = 0;
            qreal pageHeight = 0;
            summarizeElements(page.elements, &pageLines, &pageMax, &pageHeight);
            sample.lines += pageLines;
            sample.maxLines = std::max(sample.maxLines, pageMax);
            sample.maxElementHeight = std::max(sample.maxElementHeight, pageHeight);
        }
        sample.ok = sample.pages > 0 && !result.pdf.isEmpty();
    }
    if (!sample.ok && sample.error.isEmpty())
        sample.error = QStringLiteral("empty result");
    return sample;
}

QJsonObject compJson(const CompSample &sample)
{
    return QJsonObject{
        {QStringLiteral("ok"), sample.ok},
        {QStringLiteral("timeout"), sample.timeout},
        {QStringLiteral("error"), sample.error},
        {QStringLiteral("ready_ns"), sample.readyNs},
        {QStringLiteral("parse_ns"), sample.parseNs},
        {QStringLiteral("layout_ns"), sample.layoutNs},
        {QStringLiteral("pdf_ns"), sample.pdfNs},
        {QStringLiteral("reused_content"), sample.reused},
        {QStringLiteral("shape_hits"), QString::number(sample.hits)},
        {QStringLiteral("shape_misses"), QString::number(sample.misses)},
        {QStringLiteral("pages"), sample.pages},
        {QStringLiteral("elements"), sample.elements},
        {QStringLiteral("lines"), sample.lines},
        {QStringLiteral("max_lines_in_element"), sample.maxLines},
        {QStringLiteral("max_element_height"), sample.maxElementHeight},
        {QStringLiteral("content_height"), sample.height},
        {QStringLiteral("pdf_bytes"), sample.pdfBytes},
        {QStringLiteral("soft_hyphens"), sample.softHyphens},
    };
}

void printComp(const QString &label, const CompSample &sample)
{
    QTextStream out(stdout);
    out << pad(label, 22)
        << pad(sample.ok ? ms(sample.readyNs) : sample.error, 12)
        << pad(ms(sample.parseNs), 10) << pad(ms(sample.layoutNs), 10) << pad(ms(sample.pdfNs), 10)
        << pad(sample.reused ? QStringLiteral("yes") : QStringLiteral("no"), 8)
        << pad(QString::number(sample.hits), 8) << pad(QString::number(sample.misses), 8)
        << pad(QString::number(sample.pages), 7) << pad(QString::number(sample.elements), 9)
        << pad(QString::number(sample.lines), 8) << pad(QString::number(sample.maxLines), 9)
        << Qt::endl;
}

void printCompHeader()
{
    QTextStream out(stdout);
    out << pad(QStringLiteral("request"), 22) << pad(QStringLiteral("ready"), 12)
        << pad(QStringLiteral("parse"), 10) << pad(QStringLiteral("layout"), 10)
        << pad(QStringLiteral("pdf"), 10) << pad(QStringLiteral("reuse"), 8)
        << pad(QStringLiteral("hits"), 8) << pad(QStringLiteral("misses"), 8)
        << pad(QStringLiteral("pages"), 7) << pad(QStringLiteral("elements"), 9)
        << pad(QStringLiteral("lines"), 8) << pad(QStringLiteral("maxLines"), 9) << Qt::endl;
}

CompositionRequest baseRequest(const QString &markdown, bool hyphenate)
{
    CompositionRequest request;
    request.markdown = markdown;
    request.filename = QStringLiteral("bench.md");
    request.title = QStringLiteral("bench");
    request.semanticRevision = 1;
    request.language = QStringLiteral("en_US");
    request.hyphenation = hyphenate;
    request.hyphenateJustified = hyphenate;
    request.highlightCode = true;
    return request;
}

qint64 rasterPage(Poppler::Document *document, int pageNumber, qreal zoom, qint64 *cpuNs)
{
    StageTimer timer;
    timer.start();
    std::unique_ptr<Poppler::Page> page(document->page(pageNumber));
    if (page) {
        const QSizeF pageSize = page->pageSizeF();
        const int width = std::max(1, qRound(pageSize.width() * zoom));
        const int height = std::max(1, qRound(pageSize.height() * zoom));
        page->renderToImage(72.0 * width / pageSize.width(), 72.0 * height / pageSize.height(),
                            -1, -1, width, height);
    }
    if (cpuNs)
        *cpuNs = timer.cpuNsecsElapsed();
    return timer.nsecsElapsed();
}

struct ViewStudy {
    QString label;
    qint64 bytes = 0;
    CompSample webCold;
    CompSample webResize;
    CompSample printCold;
    CompSample printWide;
    QList<qint64> paintTop, paintMid, paintStripe, paintUnculled, grabTop, grabMid;
    QList<qint64> paintTopCpu, paintUnculledCpu;
    qint64 paintColdNs = 0;
    qint64 popplerLoadNs = 0;
    qint64 pageWalkNs = 0;
    qint64 sceneItemsNs = 0;
    qint64 raster0Ns = 0;
    qint64 rasterMidNs = 0;
    qint64 rasterLastNs = 0;
    qint64 raster0AgainNs = 0;
    qint64 cacheReadyNs = 0;
    bool cacheHit = false;
    bool cacheTimeout = false;
    int popplerPages = 0;
};

bool studyOne(const QString &label, const QString &markdown, bool hyphenate, int paintIterations,
              qreal zoom, int timeoutMs, ViewStudy *study)
{
    study->label = label;
    study->bytes = markdown.toUtf8().size();
    QTextStream out(stdout);
    out << QStringLiteral("\n%1  %2 bytes  hyphenation=%3\n")
               .arg(label)
               .arg(study->bytes)
               .arg(hyphenate ? QStringLiteral("on") : QStringLiteral("off"));
    printCompHeader();

    {
        CompositionController web;
        auto request = baseRequest(markdown, hyphenate);
        request.continuous = true;
        request.width = PageLayout().contentSizePoints().width();
        study->webCold = waitForComposition(web, request, timeoutMs);
        printComp(QStringLiteral("web-cold"), study->webCold);
        if (!study->webCold.ok)
            return false;
        auto *item = new WebViewItem(study->webCold.result->fonts.get(), study->webCold.result->fonts);
        auto layout = study->webCold.result->continuousLayout;
        item->setLayoutResult(std::move(layout));
        const qreal width = std::max<qreal>(200, item->boundingRect().width());
        const qreal viewHeight = 900;
        const qreal mid = std::max<qreal>(0, item->boundingRect().height() / 2 - viewHeight / 2);
        study->paintColdNs = paintWeb(item, QRectF(0, 0, width, viewHeight), false, nullptr);
        for (int i = 0; i < paintIterations; ++i) {
            qint64 cpu = 0;
            study->paintTop.append(paintWeb(item, QRectF(0, 0, width, viewHeight), false, &cpu));
            study->paintTopCpu.append(cpu);
            study->paintMid.append(paintWeb(item, QRectF(0, mid, width, viewHeight), false, nullptr));
            study->paintStripe.append(paintWeb(item, QRectF(0, mid, width, 120), false, nullptr));
            study->paintUnculled.append(paintWeb(item, QRectF(0, 0, width, viewHeight), true, &cpu));
            study->paintUnculledCpu.append(cpu);
        }
        {
            // Destroy the item before the next composition. The worker mutates
            // the same font context the painter reads, and this loop would
            // otherwise deliver paint events during that job.
            QGraphicsScene scene;
            scene.addItem(item);
            QGraphicsView view(&scene);
            view.setRenderHint(QPainter::Antialiasing, true);
            view.setRenderHint(QPainter::TextAntialiasing, true);
            view.resize(int(width) + 40, int(viewHeight));
            view.show();
            QApplication::processEvents();
            for (int i = 0; i < paintIterations; ++i) {
                StageTimer timer;
                timer.start();
                view.viewport()->grab();
                study->grabTop.append(timer.nsecsElapsed());
                const int maximum = view.verticalScrollBar()->maximum();
                view.verticalScrollBar()->setValue(maximum / 2);
                QApplication::processEvents();
                timer.start();
                view.viewport()->grab();
                study->grabMid.append(timer.nsecsElapsed());
                view.verticalScrollBar()->setValue(0);
                QApplication::processEvents();
            }
        }
        request.width += 120;
        study->webResize = waitForComposition(web, request, timeoutMs);
        printComp(QStringLiteral("web-width"), study->webResize);
        if (!study->webResize.ok)
            return false;
    }

    {
        CompositionController print;
        auto request = baseRequest(markdown, hyphenate);
        request.continuous = false;
        study->printCold = waitForComposition(print, request, timeoutMs);
        printComp(QStringLiteral("print-cold"), study->printCold);
        if (!study->printCold.ok)
            return false;
        const QByteArray pdf = study->printCold.result->pdf;
        StageTimer timer;
        timer.start();
        auto document = Poppler::Document::loadFromData(pdf);
        study->popplerLoadNs = timer.nsecsElapsed();
        if (!document) {
            QTextStream(stderr) << QStringLiteral("Poppler could not load the preview PDF\n");
            return false;
        }
        document->setRenderHint(Poppler::Document::Antialiasing, true);
        document->setRenderHint(Poppler::Document::TextAntialiasing, true);
        study->popplerPages = document->numPages();
        timer.start();
        for (int i = 0; i < document->numPages(); ++i) {
            std::unique_ptr<Poppler::Page> page(document->page(i));
            if (page)
                page->pageSizeF();
        }
        study->pageWalkNs = timer.nsecsElapsed();
        const int midPage = document->numPages() / 2;
        const int lastPage = document->numPages() - 1;
        study->raster0Ns = rasterPage(document.get(), 0, zoom, nullptr);
        study->rasterMidNs = rasterPage(document.get(), midPage, zoom, nullptr);
        study->rasterLastNs = rasterPage(document.get(), lastPage, zoom, nullptr);
        study->raster0AgainNs = rasterPage(document.get(), 0, zoom, nullptr);

        timer.start();
        QGraphicsScene scene;
        std::unique_ptr<Poppler::Page> first(document->page(0));
        const QSizeF pageSize = first ? first->pageSizeF() : QSizeF(595, 842);
        for (int i = 0; i < document->numPages(); ++i) {
            auto *pageItem = new PdfPageItem(i, pageSize, nullptr);
            scene.addItem(pageItem);
        }
        study->sceneItemsNs = timer.nsecsElapsed();

        RenderCache cache;
        cache.setDocument(document.get());
        int readyPages = 0;
        const int want = std::min(2, document->numPages());
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&cache, &RenderCache::pixmapReady, &loop, [&](int) {
            if (++readyPages >= want)
                loop.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        auto enqueue = [&](int pageNumber) {
            std::unique_ptr<Poppler::Page> page(document->page(pageNumber));
            if (!page)
                return;
            const QSizeF size = page->pageSizeF();
            RenderCache::Request req;
            req.pageNumber = pageNumber;
            req.width = std::max(1, qRound(size.width() * zoom));
            req.height = std::max(1, qRound(size.height() * zoom));
            req.dpr = 1;
            cache.requestPixmap(req);
        };
        timer.start();
        for (int i = 0; i < want; ++i)
            enqueue(i);
        timeout.start(timeoutMs);
        if (want > 0)
            loop.exec();
        study->cacheReadyNs = timer.nsecsElapsed();
        study->cacheTimeout = readyPages < want;
        if (!study->cacheTimeout && want > 0) {
            std::unique_ptr<Poppler::Page> page(document->page(0));
            const QSizeF size = page->pageSizeF();
            study->cacheHit = !cache.cachedPixmap(0, std::max(1, qRound(size.width() * zoom)),
                                                   std::max(1, qRound(size.height() * zoom)), 1).isNull();
        }
        cache.setDocument(nullptr);

        request.pageLayout.orientation = QPageLayout::Landscape;
        study->printWide = waitForComposition(print, request, timeoutMs);
        printComp(QStringLiteral("print-width"), study->printWide);
        if (!study->printWide.ok)
            return false;
    }

    out << QStringLiteral("web paint ms   cold=%1 top=%2 mid=%3 stripe=%4 unculled=%5 (cpu top=%6 unculled=%7) grabTop=%8 grabMid=%9\n")
               .arg(ms(study->paintColdNs), ms(summarize(study->paintTop).median),
                    ms(summarize(study->paintMid).median), ms(summarize(study->paintStripe).median),
                    ms(summarize(study->paintUnculled).median), ms(summarize(study->paintTopCpu).median),
                    ms(summarize(study->paintUnculledCpu).median), ms(summarize(study->grabTop).median),
                    ms(summarize(study->grabMid).median));
    out << QStringLiteral("print install  popplerLoad=%1 pageWalk=%2 sceneItems=%3 pages=%4\n")
               .arg(ms(study->popplerLoadNs), ms(study->pageWalkNs), ms(study->sceneItemsNs))
               .arg(study->popplerPages);
    out << QStringLiteral("print raster   first=%1 mid=%2 last=%3 firstAgain=%4 cacheTwo=%5 cacheHit=%6\n")
               .arg(ms(study->raster0Ns), ms(study->rasterMidNs), ms(study->rasterLastNs),
                    ms(study->raster0AgainNs),
                    study->cacheTimeout ? QStringLiteral("timeout") : ms(study->cacheReadyNs),
                    study->cacheHit ? QStringLiteral("yes") : QStringLiteral("no"));
    return true;
}

QJsonObject studyJson(const ViewStudy &study)
{
    auto samples = [](const QList<qint64> &values) {
        QJsonArray array;
        for (qint64 value : values)
            array.append(value);
        return array;
    };
    return QJsonObject{
        {QStringLiteral("label"), study.label},
        {QStringLiteral("bytes"), study.bytes},
        {QStringLiteral("web_cold"), compJson(study.webCold)},
        {QStringLiteral("web_width"), compJson(study.webResize)},
        {QStringLiteral("print_cold"), compJson(study.printCold)},
        {QStringLiteral("print_width"), compJson(study.printWide)},
        {QStringLiteral("paint_cold_ns"), study.paintColdNs},
        {QStringLiteral("paint_top_ns"), samples(study.paintTop)},
        {QStringLiteral("paint_mid_ns"), samples(study.paintMid)},
        {QStringLiteral("paint_stripe_ns"), samples(study.paintStripe)},
        {QStringLiteral("paint_unculled_ns"), samples(study.paintUnculled)},
        {QStringLiteral("paint_top_cpu_ns"), samples(study.paintTopCpu)},
        {QStringLiteral("paint_unculled_cpu_ns"), samples(study.paintUnculledCpu)},
        {QStringLiteral("grab_top_ns"), samples(study.grabTop)},
        {QStringLiteral("grab_mid_ns"), samples(study.grabMid)},
        {QStringLiteral("poppler_load_ns"), study.popplerLoadNs},
        {QStringLiteral("page_walk_ns"), study.pageWalkNs},
        {QStringLiteral("scene_items_ns"), study.sceneItemsNs},
        {QStringLiteral("raster_first_ns"), study.raster0Ns},
        {QStringLiteral("raster_mid_ns"), study.rasterMidNs},
        {QStringLiteral("raster_last_ns"), study.rasterLastNs},
        {QStringLiteral("raster_first_again_ns"), study.raster0AgainNs},
        {QStringLiteral("cache_two_pages_ns"), study.cacheReadyNs},
        {QStringLiteral("cache_hit"), study.cacheHit},
        {QStringLiteral("cache_timeout"), study.cacheTimeout},
        {QStringLiteral("poppler_pages"), study.popplerPages},
    };
}

QString markdownForShape(const QString &shape, int count)
{
    if (shape == QLatin1String("code"))
        return codeDoc(count);
    if (shape == QLatin1String("wall"))
        return wallDoc(count);
    return syntheticDoc(count);
}

int runViews(const QString &file, const QList<int> &sizes, const QStringList &shapes, bool hyphenate,
             int iterations, qreal zoom, int timeoutMs, const QString &jsonPath)
{
    QTextStream out(stdout);
    out << QStringLiteral("penelope view benchmark  paintIterations=%1 zoom=%2 timeoutMs=%3\n")
               .arg(iterations)
               .arg(zoom, 0, 'f', 2)
               .arg(timeoutMs);
    out << QStringLiteral("Times are milliseconds. ready is worker submit to installed result.\n");
    out << QStringLiteral("Web paint is one 900px viewport. cold is the first paint; later paints reuse QRawFont. unculled passes the whole item as exposedRect.\n");
    out << QStringLiteral("pageWalk opens every Poppler page, as continuous print layout does.\n");
    out << hostSnapshot().value(QStringLiteral("loadavg")).toString() << Qt::endl;
    out << hostSnapshot().value(QStringLiteral("cpu_pressure")).toString() << Qt::endl;

    QJsonArray studies;
    auto run = [&](const QString &label, const QString &markdown) {
        ViewStudy study;
        if (!studyOne(label, markdown, hyphenate, iterations, zoom, timeoutMs, &study))
            return false;
        studies.append(studyJson(study));
        return true;
    };
    if (!file.isEmpty()) {
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly)) {
            QTextStream(stderr) << QStringLiteral("cannot read %1\n").arg(file);
            return 1;
        }
        if (!run(QFileInfo(file).fileName(), QString::fromUtf8(input.readAll())))
            return 1;
    } else {
        for (const QString &shape : shapes) {
            for (int count : sizes) {
                if (!run(QStringLiteral("%1-%2").arg(shape).arg(count), markdownForShape(shape, count)))
                    return 1;
            }
        }
    }
    const QJsonObject root{
        {QStringLiteral("host"), hostSnapshot()},
        {QStringLiteral("hyphenation"), hyphenate},
        {QStringLiteral("zoom"), zoom},
        {QStringLiteral("paint_iterations"), iterations},
        {QStringLiteral("timeout_ms"), timeoutMs},
        {QStringLiteral("studies"), studies},
    };
    return writeJson(jsonPath, root) ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir config;
    if (!config.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_DATA_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(dicts);
    Q_INIT_RESOURCE(fonts);
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
    bool viewsMode = false;
    bool sizesSet = false;
    bool windowSet = false;
    bool hyphenateOff = false;
    QString shapeArg = QStringLiteral("prose");
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
            sizesSet = true;
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
        } else if (a == QStringLiteral("--views")) {
            viewsMode = true;
        } else if (a == QStringLiteral("--no-hyphenate")) {
            hyphenateOff = true;
        } else if (a == QStringLiteral("--shape") && i + 1 < args.size()) {
            shapeArg = args.at(++i);
        } else if (a == QStringLiteral("--window-ms") && i + 1 < args.size()) {
            windowSet = true;
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

    if (viewsMode) {
        if (file.isEmpty() && !sizesSet) {
            QTextStream(stderr) << QStringLiteral("--views requires --file or --sizes\n");
            return 1;
        }
        QStringList shapes;
        if (shapeArg == QLatin1String("all"))
            shapes = {QStringLiteral("prose"), QStringLiteral("code"), QStringLiteral("wall")};
        else if (shapeArg == QLatin1String("prose") || shapeArg == QLatin1String("code")
                 || shapeArg == QLatin1String("wall"))
            shapes = {shapeArg};
        else {
            QTextStream(stderr) << QStringLiteral("--shape must be prose, code, wall, or all\n");
            return 1;
        }
        const int timeoutMs = windowSet ? int(windowMs) : 180000;
        return runViews(file, sizes, shapes, !hyphenateOff, iterations, zoom, timeoutMs, jsonPath);
    }

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