# Penelope: Markdown-to-QTextDocument Mapping (Planning Stage 2)

## Key Discovery: Qt 6 + Calligra Properties Coexist Perfectly

Qt 6 has built-in markdown-aware properties on QTextBlockFormat:

| Qt Property | Hex | Purpose |
|-------------|-----|---------|
| `HeadingLevel` | 0x1070 | Heading level 0-6 |
| `BlockQuoteLevel` | 0x1080 | Block quote nesting depth |
| `BlockCodeLanguage` | 0x1090 | Fenced code block language |
| `BlockCodeFence` | 0x1091 | Fence character (backtick/tilde) |
| `BlockMarker` | 0x10A0 | Task list checkbox (Checked/Unchecked) |
| `BlockTrailingHorizontalRulerWidth` | 0x1060 | Horizontal rule |

Calligra's custom properties live at `QTextFormat::UserProperty + N` (0x100001+),
which is a completely separate range. **No conflicts.** We use Qt's native
properties for markdown semantics and Calligra's properties for rich styling
simultaneously on the same format objects.

MD4C is already installed as a system library on this machine (`/usr/lib/libmd4c.so`,
`/usr/include/md4c.h`), so we link against it directly.

---

## Named Style Set

These styles are registered with `KoStyleManager` and applied during document
construction. Each maps a markdown semantic element to a `KoParagraphStyle` or
`KoCharacterStyle` with default formatting that users can override via the style
dock.

### Paragraph Styles (KoParagraphStyle)

| Style Name | Markdown Element | Default Formatting |
|------------|-----------------|-------------------|
| `BodyText` | Paragraphs | 11pt, normal weight, 6pt space after |
| `Heading1` | `# ...` | 28pt, bold, 16pt space before, 8pt after |
| `Heading2` | `## ...` | 24pt, bold, 14pt space before, 7pt after |
| `Heading3` | `### ...` | 20pt, bold, 12pt space before, 6pt after |
| `Heading4` | `#### ...` | 16pt, bold, 10pt space before, 5pt after |
| `Heading5` | `##### ...` | 14pt, bold, 8pt space before, 4pt after |
| `Heading6` | `###### ...` | 12pt, bold+italic, 6pt space before, 4pt after |
| `BlockQuote` | `> ...` | 11pt, italic, 20pt left margin per level, left border |
| `CodeBlock` | Fenced/indented code | Monospace 10pt, background #f6f8fa, 8pt padding |
| `ListItemBullet` | `- ...` / `* ...` | 11pt, bullet disc, indent per level |
| `ListItemOrdered` | `1. ...` | 11pt, decimal numbering, indent per level |
| `HorizontalRule` | `---` / `***` | Empty block with trailing ruler |
| `TableHeader` | `\| th \|` | 11pt, bold, center-aligned, background #f0f0f0 |
| `TableBody` | `\| td \|` | 11pt, normal, per-column alignment |
| `RawHTML` | HTML blocks | 10pt monospace, hidden in reader mode |

### Character Styles (KoCharacterStyle)

| Style Name | Markdown Element | Default Formatting |
|------------|-----------------|-------------------|
| `DefaultText` | Normal text | 11pt, normal, foreground #000 |
| `Emphasis` | `*text*` / `_text_` | Italic |
| `Strong` | `**text**` | Bold (weight 700) |
| `StrongEmphasis` | `***text***` | Bold + Italic |
| `InlineCode` | `` `code` `` | Monospace, background #f0f0f0, foreground #c7254e |
| `Link` | `[text](url)` | Foreground #0366d6, underline, anchor |
| `Strikethrough` | `~~text~~` | StrikeOut |
| `Underline` | (MD4C extension) | Underline |
| `WikiLink` | `[[target]]` | Foreground #0366d6, underline, anchor |
| `ImageCaption` | Image alt text | 10pt, italic, center-aligned |

### List Styles (KoListStyle)

| Style Name | Markdown | Format |
|------------|----------|--------|
| `BulletList` | `- ` / `* ` / `+ ` | Disc at level 1, circle at 2, square at 3+ |
| `OrderedList` | `1. ` | Decimal with "." suffix, start from specified number |

---

## MD4C Parser Configuration

```cpp
MD_PARSER parser = {};
parser.abi_version = 0;
parser.flags = MD_DIALECT_GITHUB      // tables, strikethrough, task lists, autolinks
             | MD_FLAG_UNDERLINE       // _underline_ (disables _ for emphasis)
             | MD_FLAG_WIKILINKS       // [[wiki links]]
             | MD_FLAG_LATEXMATHSPANS; // $math$ (deferred rendering, store raw)
parser.enter_block = &DocumentBuilder::enterBlock;
parser.leave_block = &DocumentBuilder::leaveBlock;
parser.enter_span  = &DocumentBuilder::enterSpan;
parser.leave_span  = &DocumentBuilder::leaveSpan;
parser.text        = &DocumentBuilder::onText;
```

Note: `MD_FLAG_UNDERLINE` repurposes `_` for underline instead of emphasis. This
means emphasis must use `*` only. This is a trade-off; we may want to make it
configurable.

---

## Block Callback Mapping

### MD_BLOCK_DOC (Document Root)

```
enter: Initialize QTextCursor at start of document. Set m_isFirstBlock = true.
leave: Finalize. Apply KSyntaxHighlighting to all code blocks as a second pass.
```

### MD_BLOCK_P (Paragraph)

```
enter: if (!m_isFirstBlock) cursor.insertBlock();
       Apply BodyText paragraph style.
       If inside blockquote, also set BlockQuoteLevel and left margin.
       m_isFirstBlock = false;
leave: (nothing)
```

### MD_BLOCK_H (Heading)

```
enter: if (!m_isFirstBlock) cursor.insertBlock();
       Apply Heading{detail->level} paragraph style.
       Set QTextBlockFormat::setHeadingLevel(detail->level).
       Set block char format with heading font size and bold.
       m_isFirstBlock = false;
leave: (nothing)
```

### MD_BLOCK_QUOTE (Block Quote)

```
enter: m_blockQuoteLevel++;
leave: m_blockQuoteLevel--;
```

The block quote level is applied to every block WITHIN the quote via the
paragraph enter handlers (MD_BLOCK_P, MD_BLOCK_H, etc.) which check
`m_blockQuoteLevel > 0` and set `BlockQuoteLevel` + left margin accordingly.

### MD_BLOCK_UL (Unordered List)

```
enter: Create QTextListFormat with ListDisc style.
       Set indent = current list nesting depth.
       Push to m_listStack.
leave: Pop from m_listStack.
```

### MD_BLOCK_OL (Ordered List)

```
enter: Create QTextListFormat with ListDecimal style.
       Set indent, start number from detail->start.
       Push to m_listStack.
leave: Pop from m_listStack.
```

### MD_BLOCK_LI (List Item)

```
enter: if (!m_isFirstBlock) cursor.insertBlock();
       If m_listStack is not empty, add block to current list.
       If detail->is_task:
           Set BlockMarker to Checked or Unchecked based on detail->task_mark.
       m_isFirstBlock = false;
leave: (nothing)
```

### MD_BLOCK_CODE (Code Block)

```
enter: if (!m_isFirstBlock) cursor.insertBlock();
       Apply CodeBlock paragraph style (monospace, background).
       Set BlockCodeLanguage from detail->lang.
       Set BlockCodeFence from detail->fence_char.
       m_inCodeBlock = true;
       m_codeBlockLanguage = extract string from detail->lang attribute.
       m_isFirstBlock = false;
leave: m_inCodeBlock = false;
```

Code block text arrives as MD_TEXT_CODE with embedded `\n` for line breaks.
Each `\n` becomes a new block (cursor.insertBlock()) with the same CodeBlock
style and language property. KSyntaxHighlighting is applied after document
construction as a second pass.

### MD_BLOCK_HR (Horizontal Rule)

```
enter: if (!m_isFirstBlock) cursor.insertBlock();
       QTextBlockFormat hrFmt;
       hrFmt.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth,
                         QTextLength(QTextLength::PercentageLength, 100));
       cursor.setBlockFormat(hrFmt);
       m_isFirstBlock = false;
leave: (nothing)
```

### MD_BLOCK_HTML (Raw HTML Block)

```
enter: if (!m_isFirstBlock) cursor.insertBlock();
       Apply RawHTML paragraph style.
       Mark block as hidden in reader mode (custom property).
       m_isFirstBlock = false;
leave: (nothing)
```

### MD_BLOCK_TABLE

```
enter: Record detail->col_count.
       Build column alignment array from subsequent TH/TD details.
       Create QTextTable via cursor.insertTable(1, col_count, tableFormat).
       m_currentTable = table pointer.
       m_tableRow = -1 (incremented on first TR).
leave: m_currentTable = nullptr. Move cursor past table.
```

### MD_BLOCK_THEAD / MD_BLOCK_TBODY

```
enter: m_inTableHeader = (type == MD_BLOCK_THEAD);
leave: (nothing)
```

### MD_BLOCK_TR (Table Row)

```
enter: m_tableRow++;
       m_tableCol = 0;
       If m_tableRow > 0 (not the first row created by insertTable):
           m_currentTable->appendRows(1);
leave: (nothing)
```

### MD_BLOCK_TH / MD_BLOCK_TD (Table Cell)

```
enter: Navigate cursor to cell at (m_tableRow, m_tableCol).
       Apply TableHeader or TableBody paragraph style.
       Set cell alignment from detail->align:
           MD_ALIGN_LEFT   -> Qt::AlignLeft
           MD_ALIGN_CENTER -> Qt::AlignCenter
           MD_ALIGN_RIGHT  -> Qt::AlignRight
           MD_ALIGN_DEFAULT -> Qt::AlignLeft
       If header: apply header cell background.
leave: m_tableCol++;
```

---

## Span Callback Mapping

All span callbacks use a **format stack** pattern:

```cpp
QStack<QTextCharFormat> m_charFormatStack;
```

### MD_SPAN_EM (Emphasis / Italic)

```
enter: m_charFormatStack.push(cursor.charFormat());
       QTextCharFormat fmt;
       fmt.setFontItalic(true);
       cursor.mergeCharFormat(fmt);  // merge, not replace
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_STRONG (Bold)

```
enter: m_charFormatStack.push(cursor.charFormat());
       QTextCharFormat fmt;
       fmt.setFontWeight(QFont::Bold);
       cursor.mergeCharFormat(fmt);
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_CODE (Inline Code)

```
enter: m_charFormatStack.push(cursor.charFormat());
       Apply InlineCode character style (monospace, background).
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_A (Link)

```
enter: m_charFormatStack.push(cursor.charFormat());
       QTextCharFormat linkFmt;
       linkFmt.setAnchor(true);
       linkFmt.setAnchorHref(extract href from detail);
       linkFmt.setToolTip(extract title from detail);
       Apply Link character style (color, underline).
       cursor.mergeCharFormat(linkFmt);
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_IMG (Image)

```
enter: Extract src and title from detail.
       Begin collecting alt text (set m_collectingAltText = true).
leave: Load image from resolved path.
       Register with document->addResource(ImageResource, url, image).
       QTextImageFormat imgFmt;
       imgFmt.setName(url.toString());
       imgFmt.setProperty(QTextFormat::ImageAltText, m_altText);
       imgFmt.setProperty(QTextFormat::ImageTitle, title);
       // Check per-file metadata for size overrides
       if (metadata has size for this image):
           imgFmt.setWidth(metadata.width);
           imgFmt.setHeight(metadata.height);
       else:
           // Cap to page width
           imgFmt.setWidth(min(image.width(), pageContentWidth));
       cursor.insertImage(imgFmt);
       m_collectingAltText = false;
       m_altText.clear();
```

Note: Image alt text arrives via text callbacks BETWEEN enter_span and
leave_span for IMG. We collect it rather than inserting it as text.

### MD_SPAN_DEL (Strikethrough)

```
enter: m_charFormatStack.push(cursor.charFormat());
       QTextCharFormat fmt;
       fmt.setFontStrikeOut(true);
       cursor.mergeCharFormat(fmt);
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_U (Underline)

```
enter: m_charFormatStack.push(cursor.charFormat());
       QTextCharFormat fmt;
       fmt.setFontUnderline(true);
       cursor.mergeCharFormat(fmt);
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_WIKILINK

```
enter: m_charFormatStack.push(cursor.charFormat());
       QTextCharFormat fmt;
       fmt.setAnchor(true);
       fmt.setAnchorHref("wiki:" + extract target from detail);
       Apply WikiLink character style.
       cursor.mergeCharFormat(fmt);
leave: cursor.setCharFormat(m_charFormatStack.pop());
```

### MD_SPAN_LATEXMATH / MD_SPAN_LATEXMATH_DISPLAY

```
Deferred. Store raw LaTeX text as a custom property on the format.
Render as monospace placeholder text for now.
```

---

## Text Callback Mapping

```
MD_TEXT_NORMAL:
    if (m_collectingAltText)
        m_altText.append(text);
    else
        cursor.insertText(text);

MD_TEXT_CODE:
    if (m_inCodeBlock)
        // Split on \n, each line is a new block with CodeBlock style
        for each line:
            if not first line: cursor.insertBlock(codeBlockFormat);
            cursor.insertText(line);
    else
        cursor.insertText(text);  // inline code, format already set

MD_TEXT_BR:
    cursor.insertBlock();  // hard line break = new block
    // Re-apply current paragraph style

MD_TEXT_SOFTBR:
    cursor.insertText(" ");  // soft break = space in reader mode

MD_TEXT_ENTITY:
    Decode HTML entity to unicode character.
    cursor.insertText(decoded);

MD_TEXT_NULLCHAR:
    cursor.insertText(QChar(0xFFFD));  // replacement character

MD_TEXT_HTML:
    // In reader mode: skip or insert as hidden
    // Could optionally parse simple inline HTML tags
```

---

## DocumentBuilder Class Design

```cpp
class DocumentBuilder : public QObject {
    Q_OBJECT

public:
    DocumentBuilder(QTextDocument *document, KoStyleManager *styleManager);

    // Main entry point: parse markdown and populate document
    bool build(const QString &markdownText);

    // Set per-file metadata for image overrides, etc.
    void setFileMetadata(const QJsonObject &metadata);

    // Set base path for resolving relative image paths
    void setBasePath(const QString &basePath);

private:
    // MD4C callbacks (static, cast userdata to DocumentBuilder*)
    static int sEnterBlock(MD_BLOCKTYPE type, void *detail, void *userdata);
    static int sLeaveBlock(MD_BLOCKTYPE type, void *detail, void *userdata);
    static int sEnterSpan(MD_SPANTYPE type, void *detail, void *userdata);
    static int sLeaveSpan(MD_SPANTYPE type, void *detail, void *userdata);
    static int sText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                     void *userdata);

    // Instance handlers
    int enterBlock(MD_BLOCKTYPE type, void *detail);
    int leaveBlock(MD_BLOCKTYPE type, void *detail);
    int enterSpan(MD_SPANTYPE type, void *detail);
    int leaveSpan(MD_SPANTYPE type, void *detail);
    int onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size);

    // Style application helpers
    void applyParagraphStyle(const QString &styleName);
    void applyCharacterStyle(const QString &styleName);
    void ensureBlock();  // Insert block if not first, track m_isFirstBlock

    // MD_ATTRIBUTE string extraction
    QString extractAttribute(const MD_ATTRIBUTE &attr);

    // State
    QTextDocument *m_document;
    QTextCursor m_cursor;
    KoStyleManager *m_styleManager;
    QString m_basePath;
    QJsonObject m_fileMetadata;

    // Tracking
    QStack<QTextCharFormat> m_charFormatStack;
    QStack<QTextList*> m_listStack;
    QTextTable *m_currentTable = nullptr;
    int m_tableRow = 0;
    int m_tableCol = 0;
    int m_blockQuoteLevel = 0;
    bool m_isFirstBlock = true;
    bool m_inCodeBlock = false;
    bool m_inTableHeader = false;
    bool m_collectingAltText = false;
    QString m_altText;
    QString m_codeLanguage;

    // Paragraph style cache (avoid repeated lookups)
    QHash<QString, KoParagraphStyle*> m_paraStyleCache;
    QHash<QString, KoCharacterStyle*> m_charStyleCache;
};
```

---

## Code Block Highlighting (Second Pass)

After `DocumentBuilder::build()` completes, KSyntaxHighlighting is applied:

```cpp
class CodeBlockHighlighter {
public:
    void highlight(QTextDocument *document);

private:
    KSyntaxHighlighting::Repository m_repository;
    KSyntaxHighlighting::Theme m_theme;  // user-selected theme
};
```

The highlighter walks all blocks, finds those with `BlockCodeLanguage` set,
resolves the language to a `KSyntaxHighlighting::Definition`, and applies
`QTextCharFormat` highlighting to the code text. This is a separate pass because:

1. MD4C delivers code text before we know the complete block structure
2. KSyntaxHighlighting works on complete text, not incremental callbacks
3. Theme changes can re-run highlighting without re-parsing markdown

---

## Image Handling Model

### Loading Pipeline

```
MD4C: MD_SPAN_IMG with src="path/to/image.png"
    |
    v
DocumentBuilder: Resolve path relative to .md file location
    |
    v
QImage::load(resolvedPath)
    |
    v
Check per-file metadata for size/alignment overrides
    |
    v
document->addResource(ImageResource, QUrl("pretty://img/N"), image)
    |
    v
cursor.insertImage(imgFmt)  // inline in QTextDocument
    |
    v
Canvas: PageItem detects image format, creates parallel ImageItem
        in QGraphicsScene for interactive controls
```

### Image Properties in Per-File Metadata

```json
{
    "images": {
        "path/to/image.png": {
            "width": 400,
            "height": 300,
            "alignment": "center",
            "wrapping": "square",
            "caption": true
        }
    }
}
```

### Canvas Integration

The `QTextDocument` contains the image as an inline `QTextImageFormat`. The
canvas widget detects image positions during rendering and creates parallel
`QGraphicsPixmapItem` objects with resize handles. When the user resizes an
image via handles, the metadata is updated and the `QTextImageFormat` dimensions
are changed, triggering relayout.

---

## Calligra Style Integration

### Property ID Layout (No Conflicts)

```
Qt built-in block properties:    0x1000 - 0x10FF
Qt built-in char properties:     0x1FE0 - 0x2FFF
Qt built-in list properties:     0x3000 - 0x3FFF
Qt built-in table properties:    0x4000 - 0x4FFF
Qt built-in image properties:    0x5000 - 0x5FFF
Qt built-in special:             0x6000 - 0x7FFF
--- gap ---
Calligra character properties:   0x100001 - 0x10002A  (UserProperty+1..42)
Calligra paragraph properties:   0x100001 - 0x100059  (UserProperty+1..89)
Calligra list properties:        0x1003E8+             (UserProperty+1000+)
```

Note: Calligra character and paragraph properties share the same numeric IDs
but are stored on different format types (QTextCharFormat vs QTextBlockFormat),
so there is no actual collision.

### How the Builder Applies Styles

```cpp
void DocumentBuilder::applyParagraphStyle(const QString &styleName) {
    KoParagraphStyle *style = m_styleManager->paragraphStyle(styleName);
    if (!style) return;

    QTextBlockFormat blockFmt;
    style->applyStyle(blockFmt);       // Sets Calligra custom properties
    m_cursor.setBlockFormat(blockFmt);

    QTextCharFormat charFmt;
    style->KoCharacterStyle::applyStyle(charFmt); // Inherited char properties
    m_cursor.setBlockCharFormat(charFmt);
    m_cursor.setCharFormat(charFmt);
}
```

The style's `applyStyle()` method iterates its property map and calls
`format.setProperty(id, value)` for each one. The layout engine later reads
these properties back to determine spacing, borders, line height, etc.

---

## Footnote Design (Future)

MD4C does not support footnotes natively (not in CommonMark or GFM). Options:

1. **Pre-processing**: Scan for `[^label]` patterns before passing to MD4C,
   extract footnote definitions, replace references with inline markers.

2. **Post-processing**: After MD4C builds the document, scan for footnote
   patterns in raw text and insert `KoInlineNote` objects.

3. **Custom extension**: Fork MD4C to add footnote support (significant effort).

Recommendation: Option 1 (pre-processing) for the first implementation.
Footnotes are collected, stripped from the markdown, and inserted into the
document as `KoInlineNote` objects that the layout engine renders at page bottom.

---

## Summary: Data Flow

```
1. User opens .md file
2. Read file to QString
3. Load per-file metadata from XDG config (if exists)
4. Create fresh QTextDocument
5. Register style set with KoStyleManager (from theme + per-file overrides)
6. DocumentBuilder::build(markdownText)
   a. MD4C parses markdown, fires callbacks
   b. Callbacks drive QTextCursor operations
   c. Each block gets paragraph style applied (sets Calligra properties)
   d. Each span gets character style merged (layered formatting)
   e. Images loaded, registered as resources, inserted inline
   f. Tables created with QTextTable API
   g. Lists created with QTextList API
7. CodeBlockHighlighter::highlight(document)
   - Walks blocks with BlockCodeLanguage
   - Applies KSyntaxHighlighting theme
8. Document ready for layout engine (KoTextDocumentLayout)
9. Canvas renders paginated result
```
