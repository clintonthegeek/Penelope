# Archive

Historical record. These documents describe designs, plans and measurements
from earlier stages of the project. Some of them are still the best written
explanation of a subsystem; none of them describe the current state, and no
task should be taken from here without re-checking the code.

| Document | Period | Why it is here |
|---|---|---|
| [01-reconnaissance-report.md](01-reconnaissance-report.md) | Feb 2026 | Survey of prior art before any code existed. |
| [02-project-planning-roadmap.md](02-project-planning-roadmap.md) | Feb 2026 | The original seven-stage plan. Superseded. |
| [03-calligra-extraction-analysis.md](03-calligra-extraction-analysis.md) | Feb 2026 | The Calligra reuse path that was not taken. |
| [04-markdown-to-qtextdocument-mapping.md](04-markdown-to-qtextdocument-mapping.md) | Feb 2026 | The QTextDocument mapping replaced by the semantic model and box tree. Detailed per-callback notes on MD4C, still useful background. |
| [05-style-system-and-persistence.md](05-style-system-and-persistence.md) | Feb–Oct 2026 | Style system and settings design. |
| [06-canvas-and-interaction-design.md](06-canvas-and-interaction-design.md) | Feb 2026 | Canvas and interaction design; the mechanics changed. |
| [07-print-and-pdf-pipeline.md](07-print-and-pdf-pipeline.md) | Feb–Oct 2026 | Print and PDF pipeline design; print view now paints box pages. |
| [2026-02-15-rendering-bugs.md](2026-02-15-rendering-bugs.md) | Feb 2026 | Bug log from a pipeline review; all fixed. |
| [2026-10-03-maintenance-audit.md](2026-10-03-maintenance-audit.md) | Oct 2026 | Defect sweep and its verification coverage. |
| [2026-10-03-markoff-port-analysis.md](2026-10-03-markoff-port-analysis.md) | Oct 2026 | Proposed reader/PDF library extraction. Not adopted. |
| [2026-10-04-composition-implementation.md](2026-10-04-composition-implementation.md) | Oct 2026 | The composition worker: thread boundary, queue, caches. Still an accurate description of that subsystem. |
| [2026-10-04-legacy-renderer-removal.md](2026-10-04-legacy-renderer-removal.md) | Oct 2026 | Record of consolidating the readers and exporters. |
| [2026-10-04-performance-review.md](2026-10-04-performance-review.md) | Oct 2026 | Where the time went before the layout cursor. |
| [2026-10-04-performance-tasks.md](2026-10-04-performance-tasks.md) | Oct 2026 | Task list superseded by the ebook-reader steps. |
| [2026-10-17-ebook-reader-roadmap.md](2026-10-17-ebook-reader-roadmap.md) | Oct 2026 | The lazy-typesetting architecture. Implemented; keep for the reasoning. |
| [2026-10-17-ebook-reader-tasks.md](2026-10-17-ebook-reader-tasks.md) | Oct 2026 | The thirty steps that implemented it, all done. |
| [2026-10-17-long-document-benchmark.md](2026-10-17-long-document-benchmark.md) | Oct 2026 | The first recorded run of the view benchmark, with its analysis. The method now lives in [benchmark.md](../benchmark.md). |
| [plans/](plans/) | Feb–Mar 2026 | Twenty-six design and implementation notes for individual features (Hershey fonts, justify, PDF export, XObjects, page splitting, TOC sync, web view, header/footer designer, sessions, theme dock, release pipeline). |
| [performance/](performance/) | Oct 2026 | Raw benchmark JSON recorded during the cursor work. |
