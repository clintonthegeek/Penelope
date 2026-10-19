# Documentation

These documents describe Penelope as it is today and what comes next.

| File | What it is |
|---|---|
| [roadmap.md](roadmap.md) | The open workstreams, in priority order. Start here. |
| [tasks.md](tasks.md) | Executable steps for the roadmap. One step per change. |
| [benchmark.md](benchmark.md) | How reader performance is measured, and what to look at. |
| [08-page-setup.md](08-page-setup.md) | How pages are described: sizes, margins, page types, chrome and page numbers. |
| [archive/](archive/) | Everything historical: stage plans, design notes, audits, recorded runs, and the completed ebook-reader plan. Background only — none of it is current. |

The user-facing description of the application lives in the repository
[README](../README.md). Packaging and releasing are documented in
[packaging/README.md](../packaging/README.md).

## Keeping the paperwork honest

- A document that describes work already done belongs in `archive/`.
- A roadmap entry names the steps that close it; a step names the files it
  touches and the test that proves it.
- Claims about behaviour belong where they can be checked: the README for users,
  a step's acceptance clause for work in progress.
