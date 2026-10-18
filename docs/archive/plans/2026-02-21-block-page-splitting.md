# Block Page Splitting Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable all block types (code blocks, paragraphs, blockquotes, list items, footnote sections) to split across page boundaries instead of overflowing.

**Architecture:** Add `splitBlockBox()` and `splitFootnoteSection()` to the layout engine, integrate them into `assignToPages()`, and update the PDF renderer to handle fragment flags for markdown fence emission.

**Tech Stack:** C++/Qt6, FreeType, existing layout engine

---

### Task 1: Add fragment flags to BlockBox

**Files:**
- Modify: `src/layout/layoutengine.h:103-145`

**Step 1: Add isFragmentStart and isFragmentEnd fields to BlockBox**

In `layoutengine.h`, add two fields after `isListItem` (line 141):

```cpp
    bool isListItem = false;

    // Fragment flags for blocks split across pages
    bool isFragmentStart = true;   // first or only fragment (emit opening fence)
    bool isFragmentEnd = true;     // last or only fragment (emit closing fence/separator)
```

**Step 2: Build to verify no compilation errors**

Run: `make -C build -j$(nproc)`
Expected: Clean build. Defaults are true/true so all existing code behaves identically.

**Step 3: Commit**

```
feat: add fragment flags to BlockBox for page splitting
```

---

### Task 2: Implement splitBlockBox()

**Files:**
- Modify: `src/layout/layoutengine.h` (add declaration inside `namespace Layout`)
- Modify: `src/layout/layoutengine.cpp` (add implementation before `assignToPages`)

**Step 1: Declare splitBlockBox**

Add the free function declaration after the `FootnoteSectionBox` struct and before the `PageElement` using-declaration (~line 205 in layoutengine.h):

```cpp
// Split a block at a line boundary to fit within availableHeight.
// Returns nullopt if the block can't be split (too few lines, or it fits already).
// minLines: minimum lines per fragment (orphan/widow protection).
std::optional<std::pair<BlockBox, BlockBox>>
splitBlockBox(const BlockBox &block, qreal availableHeight, int minLines = 2);
```

Also add `#include <optional>` at the top of layoutengine.h if not already present.

**Step 2: Implement splitBlockBox**

Add the implementation in layoutengine.cpp, before `Engine::assignToPages()` (~line 1558). The function is a free function in the `Layout` namespace:

```cpp
std::optional<std::pair<BlockBox, BlockBox>>
splitBlockBox(const BlockBox &block, qreal availableHeight, int minLines)
{
    // Can't split blocks without lines (images, hrules)
    if (block.lines.isEmpty())
        return std::nullopt;

    int totalLines = block.lines.size();

    // Need at least minLines*2 to satisfy orphan+widow
    if (totalLines < minLines * 2)
        return std::nullopt;

    // Walk lines to find split point.
    // For code blocks (and blocks with padding), the available height for lines
    // is reduced by padding on top and bottom of each fragment.
    qreal paddingOverhead = (block.type == BlockBox::CodeBlockType) ? block.padding * 2 : 0;
    qreal availForLines = availableHeight - block.spaceBefore - paddingOverhead;
    if (availForLines <= 0)
        return std::nullopt;

    int splitAfter = 0; // number of lines in fragment 1
    qreal accum = 0;
    for (int i = 0; i < totalLines; ++i) {
        accum += block.lines[i].height;
        if (accum <= availForLines)
            splitAfter = i + 1;
        else
            break;
    }

    // Enforce orphan minimum (fragment 1 must have >= minLines)
    if (splitAfter < minLines)
        return std::nullopt;

    // Enforce widow minimum (fragment 2 must have >= minLines)
    if (totalLines - splitAfter < minLines)
        return std::nullopt;

    // Build fragment 1: lines [0..splitAfter)
    BlockBox frag1 = block;
    frag1.lines = block.lines.mid(0, splitAfter);
    frag1.isFragmentEnd = false;
    frag1.spaceAfter = 0;
    // Recompute height from lines
    qreal h1 = 0;
    for (int i = 0; i < splitAfter; ++i)
        h1 += block.lines[i].height;
    frag1.height = h1 + paddingOverhead;

    // Build fragment 2: lines [splitAfter..end)
    BlockBox frag2 = block;
    frag2.lines = block.lines.mid(splitAfter);
    frag2.isFragmentStart = false;
    frag2.spaceBefore = 0;
    frag2.firstLineIndent = 0; // continuation has no indent
    // Recompute height from lines
    qreal h2 = 0;
    for (int i = splitAfter; i < totalLines; ++i)
        h2 += block.lines[i].height;
    frag2.height = h2 + paddingOverhead;

    return std::make_pair(frag1, frag2);
}
```

**Step 3: Build to verify**

Run: `make -C build -j$(nproc)`
Expected: Clean build. The function is not called yet.

**Step 4: Commit**

```
feat: implement splitBlockBox for page-breaking blocks at line boundaries
```

---

### Task 3: Implement splitFootnoteSection()

**Files:**
- Modify: `src/layout/layoutengine.h` (add declaration)
- Modify: `src/layout/layoutengine.cpp` (add implementation)

**Step 1: Declare splitFootnoteSection**

Add after the `splitBlockBox` declaration in layoutengine.h:

```cpp
std::optional<std::pair<FootnoteSectionBox, FootnoteSectionBox>>
splitFootnoteSection(const FootnoteSectionBox &box, qreal availableHeight);
```

**Step 2: Implement splitFootnoteSection**

Add in layoutengine.cpp, after `splitBlockBox`:

```cpp
std::optional<std::pair<FootnoteSectionBox, FootnoteSectionBox>>
splitFootnoteSection(const FootnoteSectionBox &box, qreal availableHeight)
{
    if (box.footnotes.size() < 2)
        return std::nullopt;

    // Account for separator height (~10pt) and spacing
    qreal separatorHeight = box.showSeparator ? 10.0 : 0;
    qreal availForFootnotes = availableHeight - separatorHeight;
    if (availForFootnotes <= 0)
        return std::nullopt;

    int splitAfter = 0;
    qreal accum = 0;
    for (int i = 0; i < box.footnotes.size(); ++i) {
        accum += box.footnotes[i].height;
        if (accum <= availForFootnotes)
            splitAfter = i + 1;
        else
            break;
    }

    if (splitAfter == 0 || splitAfter == box.footnotes.size())
        return std::nullopt;

    FootnoteSectionBox frag1 = box;
    frag1.footnotes = box.footnotes.mid(0, splitAfter);
    qreal h1 = separatorHeight;
    for (int i = 0; i < splitAfter; ++i)
        h1 += box.footnotes[i].height;
    frag1.height = h1;

    FootnoteSectionBox frag2;
    frag2.footnotes = box.footnotes.mid(splitAfter);
    frag2.showSeparator = false;
    frag2.width = box.width;
    // Rebase footnote y-positions
    qreal yOff = 0;
    for (auto &fn : frag2.footnotes) {
        fn.y = yOff;
        yOff += fn.height;
    }
    frag2.height = yOff;

    return std::make_pair(frag1, frag2);
}
```

**Step 3: Build to verify**

Run: `make -C build -j$(nproc)`
Expected: Clean build.

**Step 4: Commit**

```
feat: implement splitFootnoteSection for page-breaking footnotes
```

---

### Task 4: Integrate block splitting into assignToPages()

**Files:**
- Modify: `src/layout/layoutengine.cpp:1560-1711` (the `assignToPages` method)

This is the core integration. The current loop iterates a `const QList<PageElement>`. To support pushing fragment2 back into the queue, change to an index-based loop over a mutable copy.

**Step 1: Replace the element iteration with a mutable queue**

Change `assignToPages` to copy elements into a mutable `QList<PageElement>` and use index-based iteration that allows insertion:

Replace the for-loop (line 1571) from:
```cpp
    for (int idx = 0; idx < elements.size(); ++idx) {
        const auto &element = elements[idx];
```

To:
```cpp
    QList<PageElement> queue = elements;

    for (int idx = 0; idx < queue.size(); ++idx) {
        const auto &element = queue[idx];
```

And update all remaining references from `elements` to `queue` within the loop body (there are two: `elements[idx + 1]` at the keepWithNext peek).

**Step 2: Add block splitting after the page-break decision**

After the existing page-break logic (after line 1682, where `y = 0` on the new page), add block splitting for the case where the block is placed on an empty page but still doesn't fit. Also add splitting for the case where we CAN split instead of doing a full page break.

Replace the section from `if (needsPageBreak)` through to the element positioning (lines 1676-1698) with:

```cpp
        // --- Block splitting logic ---
        // Try to split the block if it doesn't fit, rather than always
        // pushing the whole block to the next page.
        if (needsPageBreak) {
            // Before breaking to a new page, try splitting at the break point
            qreal remaining = pageHeight - y;
            bool didSplit = false;

            if (auto *bb = std::get_if<BlockBox>(&element)) {
                auto split = splitBlockBox(*bb, remaining);
                if (split) {
                    // Place fragment 1 on current page
                    auto &[f1, f2] = *split;
                    y += f1.spaceBefore;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height + f1.spaceAfter;
                    // Push fragment 2 back into queue
                    queue.insert(idx + 1, f2);
                    didSplit = true;
                }
            } else if (auto *fs = std::get_if<FootnoteSectionBox>(&element)) {
                auto split = splitFootnoteSection(*fs, remaining);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += 20.0; // spaceBefore for footnote sections
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height;
                    queue.insert(idx + 1, f2);
                    didSplit = true;
                }
            }

            if (didSplit) {
                // Fragment 1 placed; fragment 2 will be processed next iteration
                continue;
            }

            // Split failed — break to new page (existing behavior)
            currentPage.contentHeight = y;
            result.pages.append(currentPage);
            currentPage = Page{};
            currentPage.pageNumber = result.pages.size();
            y = 0;
        }

        // Handle blocks that don't fit even on a fresh empty page
        if (currentPage.elements.isEmpty() && y + totalHeight > pageHeight) {
            bool didSplit = false;

            if (auto *bb = std::get_if<BlockBox>(&element)) {
                auto split = splitBlockBox(*bb, pageHeight);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += f1.spaceBefore;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height + f1.spaceAfter;
                    queue.insert(idx + 1, f2);
                    continue;
                }
            } else if (auto *fs = std::get_if<FootnoteSectionBox>(&element)) {
                auto split = splitFootnoteSection(*fs, pageHeight);
                if (split) {
                    auto &[f1, f2] = *split;
                    y += 20.0;
                    f1.y = y;
                    currentPage.elements.append(f1);
                    y += f1.height;
                    queue.insert(idx + 1, f2);
                    continue;
                }
            }
            // If split failed, fall through to place-and-overflow (existing behavior)
        }

        y += spaceBefore;

        // Set element position
        PageElement positioned = element;
        std::visit([&](auto &e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlockBox>) {
                e.y = y;
            } else if constexpr (std::is_same_v<T, FootnoteSectionBox>) {
                e.y = y;
            }
        }, positioned);

        currentPage.elements.append(positioned);
        y += elementHeight + spaceAfter;
```

**Step 3: Update the orphan protection**

The existing orphan protection (lines 1657-1674) pushes a whole block to the next page if < 2 lines fit. This is now redundant with splitBlockBox's minLines check — splitBlockBox returns nullopt if < 2 lines would fit, which causes the existing page-break to trigger. **Remove the orphan protection block entirely** — it's subsumed by the split logic.

**Step 4: Build and test**

Run: `make -C build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: Clean build, tests pass.

**Step 5: Commit**

```
feat: integrate block and footnote splitting into page assignment
```

---

### Task 5: Update PDF renderer for fragment-aware markdown fences

**Files:**
- Modify: `src/pdf/pdfgenerator.cpp:460-527` (the fence emission in `renderBlockBox`)

**Step 1: Gate opening fence on isFragmentStart**

Change line 466 from:
```cpp
    if (m_exportOptions.markdownCopy && isCodeBlock) {
```
To:
```cpp
    if (m_exportOptions.markdownCopy && isCodeBlock && box.isFragmentStart) {
```

**Step 2: Gate closing fence and block separator on isFragmentEnd**

Change the block separator section (line 507) from:
```cpp
    if (m_exportOptions.markdownCopy) {
```
To:
```cpp
    if (m_exportOptions.markdownCopy && box.isFragmentEnd) {
```

**Step 3: Gate m_codeBlockLines flag on fragment position**

The `m_codeBlockLines` flag controls whether line-level ActualText adds trailing `\n`. For code block fragments:
- `isFragmentStart`: set `m_codeBlockLines = true` (line 467)
- Non-start fragments also need `m_codeBlockLines = true` while rendering lines
- `isFragmentEnd`: clear `m_codeBlockLines = false` (line 499)

Change line 466-467 from:
```cpp
    if (m_exportOptions.markdownCopy && isCodeBlock && box.isFragmentStart) {
        m_codeBlockLines = true;
```
To:
```cpp
    if (m_exportOptions.markdownCopy && isCodeBlock)
        m_codeBlockLines = true;
    if (m_exportOptions.markdownCopy && isCodeBlock && box.isFragmentStart) {
```

And change line 499 from:
```cpp
    m_codeBlockLines = false;
```
To:
```cpp
    if (isCodeBlock && box.isFragmentEnd)
        m_codeBlockLines = false;
```

**Step 4: Build and test**

Run: `make -C build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: Clean build, tests pass.

**Step 5: Commit**

```
feat: update PDF renderer for fragment-aware markdown fence emission
```

---

### Task 6: Visual testing and edge case verification

**Files:** None (manual testing)

**Step 1: Test code block splitting**

Export a PDF containing a code block longer than one page with xobjectGlyphs off. Verify:
- Code block splits across pages with independent background/border on each fragment
- Syntax highlighting continues correctly on the second page
- Right-click on either fragment opens the language picker for the correct block

**Step 2: Test code block splitting with markdown copy**

Export with markdownCopy enabled. Select all text and paste. Verify:
- Opening ``` fence appears once at the start
- Closing ``` fence appears once at the end
- No duplicate fences at page boundaries
- Code content is continuous

**Step 3: Test paragraph splitting**

Create a document with a very long paragraph (paste lorem ipsum). Verify it splits cleanly across pages.

**Step 4: Test blockquote splitting**

Create a blockquote containing many lines. Verify:
- Each fragment shows the left border
- Background continues on each fragment

**Step 5: Test XObject glyph mode**

Repeat code block test with xobjectGlyphs enabled. Verify rendering is identical.

**Step 6: Test short blocks (no split)**

Verify that blocks with < 4 lines that don't fit are pushed to the next page (not split into fragments of 1-2 lines).

**Step 7: Commit any fixes found during testing**

---

## Execution Notes

- Tasks 1-3 are independent struct/function additions — safe to parallelize.
- Task 4 is the core integration — depends on tasks 1-3.
- Task 5 depends on task 1 (needs fragment flags).
- Task 6 is manual testing after everything is integrated.
