# Tasks

Every step this file has carried is implemented. The evidence column names the
regression slot that proves it, all of which pass in
`QT_QPA_PLATFORM=offscreen ctest --test-dir build`.

Work goes in the same shape as before: one step per change; the step names the
files it touches, what to do, what not to do, the test slot to add, and the
observable acceptance. Assert structure — how many blocks were laid out, whether
a cache hit, whether a cancelled request returned early — never wall-clock
milliseconds. Use a `RelWithDebInfo` build for speed comparisons
([benchmark.md](benchmark.md)).

## Interim alignment with markoff-core

| Id | Step | Evidence |
|---|---|---|
| AL-01 | Front matter is metadata: `markoff-parser`'s rule, lines blanked so source ranges still address the file | `al01FrontmatterIsMetadata` |
| AL-02 | `_underscore_` is emphasis; the underline extension is gone | `al02UnderscoreIsEmphasis` |
| AL-03 | Unreferenced footnote definitions are dropped | `al03UnreferencedNotesAreDropped` |
| AL-04 | Clipboard copy uses the note's printed marker, not a 0-based index | `al04CopyUsesNoteMarkers` |
| AL-05 | Links keep href, title, kind; `[[Page\|label]]` records its target; `![[Note]]` is an embed; `#tag` classifies | `al05LinkTargetsAndTitles` |
| AL-06 | Raw HTML is kept, not deleted | `al06HtmlIsKept` |

## Footnotes

| Id | Step | Evidence |
|---|---|---|
| FN-01 | A note body keeps its Markdown structure (blocks, not one text run) | `fn01FootnoteBlocks` |
| FN-02 | A note records where it is cited; repeats are marked | `fn02ReferenceAnchors` |
| FN-03 | Numbering lives in the layout, resolved through `Content::footnoteMarker` | `fn03NumberingIsLayoutOwned` |
| FN-04 | Notes print in a band at the foot of the page that cites them, with carried overflow | `fn04PageBottomNotes`, `fn04NoteOverflow` |
| FN-05 | Numbering can restart on each page | `fn05PerPageNumbering` |
| FN-06 | Citations and markers navigate both ways and hover | `fn06FootnoteHoverAndBacklink` |

## Markdown dialect

| Id | Step | Evidence |
|---|---|---|
| MD-01 | Math spans are modelled; inline math flows, display math is centred | `md01MathSpans` |
| MD-02 | Raw HTML is set as markup rather than prose | `md02RawHtmlIsMarkedUp` |
| MD-03 | Wikilink and embed targets reach the model | `al05LinkTargetsAndTitles` |
| MD-04 | Inline images survive headings, table cells and list items | `md04InlineImagesAwayFromParagraphs` |
| MD-05 | The full HTML entity table decodes; unknown names stay literal | `md05NamedEntities` |
| MD-06 | Optional smart punctuation, with code untouched | `md06SmartPunctuation` |
| MD-07 | Image titles are hover hints | `md07ImageTitles` |
| MD-08 | `)` markers, list tightness and fence info strings are preserved | `md08ListAndFenceDetails` |
| MD-09 | Front matter policy (AL-01); definition lists are not planned | `al01FrontmatterIsMetadata` |

## Quality

| Id | Step | Evidence |
|---|---|---|
| Q-01 | Styled copy stops at the selection, from the shaped runs | `q01SelectionCopyIsClipped` |
| Q-02 | Cluster-accurate PDF text: ligatures spell out, RTL extracts, missing scripts fall back | `q02PdfTextSemantics` |
| Q-03 | The reader answers assistive technology block by block | `q03ReaderIsAccessible` |
| Q-04 | One ICU line-break iterator per engine | `q04BreakIteratorIsReused` |
| Q-05 | Each image decodes once per engine | `q05ImagesAreDecodedOnce` |
| Q-06 | The shaping cache budget holds prose and refuses one huge fence | `q06ShapingCacheBudget` |
| Q-07 | One `QRawFont` per face and size, shared by every renderer | `q07RawFontsAreShared` |
| Q-08 | Presentation-only editor changes schedule no rebuild | `sourceEditSignalIgnoresPresentationChanges` |

## Deliberately not done

| What | Why |
|---|---|
| LaTeX *typesetting* (MD-01's engine) | jkqtmathtext is 1.5 MB of source plus 12 MB of bundled math fonts and needs Qt 6.8 where Penelope declares 6.6. Markoff carries it; the rebase brings it. Math is set, not typeset, until then. |
| Character-level selection (Q-01's full form) | Penelope's source map is line-based and block-relative byte ranges arrive with `markoff-parser`. The styled copy is run-accurate today. |
| Styled copy in the PDF fallback path | There is no display list to read in that mode; it keeps the block-based extraction. |
| Definition lists (MD-09) | Neither MD4C nor Markoff's parser has them. |
| A streaming Markdown parser, replacing MD4C/HarfBuzz/the PDF writer | The first screen may wait for the parse; it is the smaller floor. |
| Reflowing print view on window resize | That is the PDF-reader behaviour we keep. |
