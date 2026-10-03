#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
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

#include <poppler-qt6.h>

#include "contentbuilder.h"
#include "documenttab.h"
#include "fontmanager.h"
#include "layoutengine.h"
#include "mainwindow.h"
#include "pdfgenerator.h"
#include "penelopesettings.h"
#include "textshaper.h"

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

struct StageTimes {
    qint64 parseNs = 0;
    qint64 layoutNs = 0;
    qint64 continuousNs = 0;
    qint64 pdfNs = 0;
    qint64 rasterNs = 0;
    int pages = 0;
    int blocks = 0;
    qint64 openNs() const { return parseNs + layoutNs + pdfNs; }
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
                       bool measureRaster, qreal zoom, int compressLevel, int runTextMode)
{
    StageTimes t;
    FontManager fonts;

    QElapsedTimer timer;

    ContentBuilder builder;
    builder.setFootnoteStyle(FootnoteStyle());
    timer.start();
    Content::Document doc = builder.build(markdown);
    t.parseNs = timer.nsecsElapsed();
    t.blocks = int(doc.blocks.size());

    TextShaper shaper(&fonts);
    Layout::Engine engine(&fonts, &shaper);
    engine.setHyphenateJustifiedText(hyphenate);
    PageLayout pl;

    fonts.resetUsage();
    timer.start();
    Layout::LayoutResult paginated = engine.layout(doc, pl);
    t.layoutNs = timer.nsecsElapsed();
    t.pages = int(paginated.pages.size());

    if (measureContinuous) {
        fonts.resetUsage();
        timer.start();
        Layout::ContinuousLayoutResult continuous =
            engine.layoutContinuous(doc, pl.contentSizePoints().width());
        t.continuousNs = timer.nsecsElapsed();
        Q_UNUSED(continuous);
    }

    // No resetUsage() here: the shaper marks glyphs during layout and
    // generate() embeds the subset from that set, exactly as the reader does.
    PdfGenerator generator(&fonts);
    generator.setCompressionLevel(compressLevel);
    if (runTextMode >= 0)
        generator.setRunTextOperators(runTextMode == 1);
    timer.start();
    QByteArray pdf = generator.generate(paginated, pl, QStringLiteral("bench"));
    t.pdfNs = timer.nsecsElapsed();

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
        "Continuous layout shares the shaping cache with the paginated path but "
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
    int pages = 0;
    int blocks = 0;
};

Row runRow(const QString &label, const QString &markdown, int iterations, bool hyphenate,
           bool measureContinuous, bool measureRaster, qreal zoom, int compressLevel, int runTextMode)
{
    measureOnce(markdown, hyphenate, measureContinuous, measureRaster, zoom, compressLevel, runTextMode);

    QList<qint64> parse, layout, continuous, pdf, raster, open;
    Row row;
    row.label = label;
    row.bytes = markdown.toUtf8().size();

    for (int i = 0; i < iterations; ++i) {
        StageTimes t = measureOnce(markdown, hyphenate, measureContinuous, measureRaster, zoom, compressLevel, runTextMode);
        parse.append(t.parseNs);
        layout.append(t.layoutNs);
        continuous.append(t.continuousNs);
        pdf.append(t.pdfNs);
        raster.append(t.rasterNs);
        open.append(t.openNs());
        row.pages = t.pages;
        row.blocks = t.blocks;
    }

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
    qint64 busyNs = 0;
    qint64 maxBlockNs = 0;
    int blockCount = 0;
    int turns = 0;
    int tabs = 0;
};

OpenResult measureOpen(const QString &path, bool webMode, qint64 windowNs)
{
    OpenResult r;

    PenelopeSettings::self()->setUsePdfRenderer(true);
    PenelopeSettings::self()->setUseWebView(webMode);
    PenelopeSettings::self()->setAutoReloadOnChange(false);

    MainWindow window;
    window.resize(1000, 800);
    window.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);

    QElapsedTimer timer;
    timer.start();

    QElapsedTimer callTimer;
    callTimer.start();
    window.openFile(QUrl::fromLocalFile(path));
    r.openCallNs = callTimer.nsecsElapsed();

    while (timer.nsecsElapsed() < windowNs) {
        const qint64 before = timer.nsecsElapsed();
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 gap = timer.nsecsElapsed() - before;

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
    qint64 toFirstPaintNs = 0;
    qint64 toSettledNs = 0;
    qint64 maxBlockNs = 0;
    int blockCount = 0;
};

// Measures what a keystroke actually costs: from inserting one character to
// the reader going quiet again. Includes the rebuild debounce, so this is the
// latency a user perceives while typing rather than a single pipeline pass.
RebuildResult measureRebuild(const QString &path, bool webMode, qint64 windowNs)
{
    RebuildResult r;

    PenelopeSettings::self()->setUsePdfRenderer(true);
    PenelopeSettings::self()->setUseWebView(webMode);
    PenelopeSettings::self()->setAutoReloadOnChange(false);

    MainWindow window;
    window.resize(1000, 800);
    window.show();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    window.openFile(QUrl::fromLocalFile(path));

    QElapsedTimer settle;
    settle.start();
    while (settle.nsecsElapsed() < 1'500'000'000LL)
        QApplication::processEvents(QEventLoop::AllEvents, 5);

    auto *tab = window.findChild<DocumentTab *>();
    if (!tab)
        return r;

    if (auto *action = window.actionCollection()->action(QStringLiteral("view_source_mode")))
        action->trigger();
    QApplication::processEvents(QEventLoop::AllEvents, 20);

    QElapsedTimer timer;
    timer.start();
    tab->sourceEditor()->insertPlainText(QStringLiteral("x"));

    qint64 lastBusy = 0;
    bool painted = false;
    while (timer.nsecsElapsed() < windowNs) {
        const qint64 before = timer.nsecsElapsed();
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        const qint64 gap = timer.nsecsElapsed() - before;

        if (gap > 1'000'000) {
            lastBusy = timer.nsecsElapsed();
            if (!painted) {
                r.toFirstPaintNs = timer.nsecsElapsed();
                painted = true;
            }
            if (gap > 20'000'000) {
                r.maxBlockNs = std::max(r.maxBlockNs, gap);
                ++r.blockCount;
            }
        } else if (painted && timer.nsecsElapsed() - lastBusy > 400'000'000LL) {
            break;
        }
    }
    r.toSettledNs = lastBusy > 0 ? lastBusy : timer.nsecsElapsed();
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
    cols << pad(ms(r.open.median), 9);
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
    cols << pad(QStringLiteral("open"), 9);
    out << cols.join(QLatin1Char('\t')) << Qt::endl;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("penelope_renderbench"));
    QStandardPaths::setTestModeEnabled(true);

    QStringList args = QApplication::arguments();
    QString file;
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
        if (a == QStringLiteral("--file") && i + 1 < args.size()) {
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
        out << QStringLiteral("mode=rebuild  one keystroke, including the rebuild debounce\n")
            << Qt::endl;
        out << pad(QStringLiteral("view"), 8) << pad(QStringLiteral("firstPaint"), 11)
            << pad(QStringLiteral("settled"), 10) << pad(QStringLiteral("maxStall"), 10)
            << pad(QStringLiteral("stalls"), 8) << Qt::endl;
        for (bool web : {false, true}) {
            RebuildResult r = measureRebuild(file, web, windowMs * 1'000'000LL);
            out << pad(web ? QStringLiteral("web") : QStringLiteral("page"), 8)
                << pad(ms(r.toFirstPaintNs), 11) << pad(ms(r.toSettledNs), 10)
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
                outFile.open(QIODevice::WriteOnly);
                outFile.write(pdf);
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
        out << QStringLiteral("mode=open  window=%1ms  openCall=sync openFile cost, busy=post-open event-loop work, stalls>20ms\n")
                   .arg(windowMs)
            << Qt::endl;
        out << pad(QStringLiteral("view"), 8) << pad(QStringLiteral("openCall"), 10)
            << pad(QStringLiteral("busy"), 10) << pad(QStringLiteral("total"), 10)
            << pad(QStringLiteral("maxStall"), 10) << pad(QStringLiteral("stalls"), 8)
            << pad(QStringLiteral("tabs"), 6) << Qt::endl;

        for (bool web : {false, true}) {
            OpenResult r = measureOpen(file, web, windowMs * 1'000'000LL);
            out << pad(web ? QStringLiteral("web") : QStringLiteral("page"), 8)
                << pad(ms(r.openCallNs), 10) << pad(ms(r.busyNs), 10)
                << pad(ms(r.openCallNs + r.busyNs), 10) << pad(ms(r.maxBlockNs), 10)
                << pad(QString::number(r.blockCount), 8) << pad(QString::number(r.tabs), 6)
                << Qt::endl;
        }
        return 0;
    }

    QTextStream out(stdout);
    out << QStringLiteral("penelope render benchmark  (iterations=%1, hyphenate=%2)\n")
               .arg(iterations)
               .arg(hyphenate ? QStringLiteral("on") : QStringLiteral("off"));
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
                           raster, zoom, compressLevel, runTextMode));
    } else {
        for (int n : std::as_const(sizes)) {
            const QString md = syntheticDoc(n);
            rows.append(runRow(QStringLiteral("synthetic-%1p").arg(n), md, iterations, hyphenate,
                               continuous, raster, zoom, compressLevel, runTextMode));
        }
    }

    printHeader(continuous, raster);
    for (const Row &r : std::as_const(rows))
        printRow(r, continuous, raster);

    return 0;
}