# Legacy renderer removal

The continuous reader already uses ContentBuilder → Layout::Engine → QtBoxRenderer.
The paginated reader uses the same content/layout pipeline, then PdfGenerator →
Poppler. Neither needs the old QTextDocument Markdown reader.

Removed:

- DocumentBuilder and its separate code-block highlighter.
- PageItem and DocumentView's QTextDocument layout/navigation branches.
- QTextDocument RTF export and printer pagination/PDF export.
- Legacy table-of-contents construction and block-number navigation.
- The rendering-engine preference and UsePdfRenderer configuration schema entry.
- Unused QTextCharFormat/QTextBlockFormat adapters in the style classes and the
  old painter header/footer implementation.

PrintController still prints the generated PDF through Poppler. Header/footer
field expansion remains shared with PDF generation. QTextDocument remains a Qt
implementation detail of the source editor and syntax highlighter.

An existing UsePdfRenderer=false preference is ignored. Web/Print mode selection,
page templates, source editing, exports and saved view state remain supported.
No migration of document files or metadata is needed.

The hidden clipboard dependency on DocumentBuilder has been replaced with
Content::toPlainText. It traverses the shared content model, preserving visible
text, nested blocks, image descriptions and notes. Tables copy as tab-separated
rows, and layout-only soft hyphens/non-breaking spaces are normalized. Clipboard
conversion disables image loading.

The removal also exposed a navigation gap: goToPage now rebuilds all three
non-continuous arrangements, including facing pages with the first page alone.

Validation includes the retired preference set to false during a Print → Web →
Print test, clipboard extraction for nested lists/tables/code/images/notes,
saved page/zoom restoration and page navigation in all non-continuous arrangements.

The optimized build passed, along with all three CTest targets and 42 QtTest
cases (zero failures/skips). `git diff --check` passed. Tests run offscreen;
physical printer output was not exercised.
