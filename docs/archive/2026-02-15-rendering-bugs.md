# Rendering Pipeline Bug Log

Found during full pipeline review (2026-02-15). All bugs fixed in this commit.

## BUG-1: Nested tight-list text goes to parent's implicit paragraph (CRITICAL) — FIXED

**Fix:** Architectural restructuring. Replaced flat `m_inListItem` boolean and
`m_listItemHasImplicitParagraph` with per-list-level `hasImplicitParagraph` in
`ListInfo`. `enterBlock(UL/OL)` now closes parent item's implicit paragraph
before pushing nested list. Nested lists are structurally placed inside parent
`ListItem::children` instead of as top-level blocks with a `depth` field.

## BUG-2: `m_inListItem` is a flat boolean, not nesting-aware — FIXED

**Fix:** Removed `m_inListItem` entirely. List context is determined by
`!m_listStack.isEmpty()`. Nesting is structural — `leaveBlock(UL/OL)` checks
the list stack after popping to decide whether to nest in parent or add to doc.

## BUG-3: Task list checkboxes not rendered — FIXED

**Fix:** `layoutList()` now checks `item.isTask` and `item.taskChecked` to
prepend checkbox glyphs (U+2611 checked, U+2610 unchecked) instead of bullets.

## BUG-4: Bullet style inheritance only handles TextRun — FIXED

**Fix:** Replaced type-specific `std::visit` with C++20 `requires` constraint
that matches any inline variant member with a `.style` field.

## BUG-5: Unused `advance` variable in header/footer rendering — FIXED

**Fix:** Extracted `measureText()` and `emitText()` helpers in
`renderHeaderFooter()`. The dead `advance` variable is gone.

## BUG-6: Header/footer right-alignment uses hardcoded offset — FIXED

**Fix:** Right and center fields now measure actual text width via
`measureText()` and compute proper alignment positions.

## BUG-7: `enterBlock(P)` allocates Content::Paragraph on heap unnecessarily — FIXED

**Fix:** Refactored to resolve `ParagraphFormat` on the stack, then construct
the `Content::Paragraph` directly in the target container. No heap allocation.

## BUG-8: Potential dangling pointer from implicit paragraph — FIXED

**Fix:** Resolved by BUG-1/BUG-2 fix. `enterBlock(UL/OL)` closes the implicit
paragraph (pops inline stack) before the nested list can append to the same
`children` QList. The pointer is off the stack before any reallocation.

## Architectural change: Removed `Content::List::depth`

Nesting depth is no longer stored — it's implicit from structural nesting.
`Layout::Engine::layoutList()` and `ContentRtfExporter::writeList()` take an
explicit `int depth` parameter (default 0) and pass `depth + 1` to recursive
calls.
