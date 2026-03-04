# Penelope: Style System & Persistence Design (Planning Stage 3)

## Storage Architecture

```
~/.config/Penelope/
    peneloperc              KConfigXT (global settings singleton)
    file-metadata.json          Per-file display state (URL-keyed)

~/.local/share/Penelope/
    themes/
        default.json            Bundled theme
        academic.json           Bundled theme
        manuscript.json         Bundled theme
        casual.json             Bundled theme
        user-*.json             User-created themes
```

| Concern | Format | Managed By |
|---------|--------|-----------|
| Global settings (default theme, zoom, page size, toggles) | KConfig INI | KConfigXT codegen singleton |
| Theme definitions (all named style properties) | JSON | ThemeManager class |
| Per-file display state (scroll, zoom, theme override, image overrides) | JSON | FileMetadataStore class |

---

## Global Settings: KConfigXT Schema

File: `src/app/penelope.kcfg`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<kcfg xmlns="http://www.kde.org/standards/kcfg/1.0">
  <kcfgfile name="peneloperc"/>

  <group name="General">
    <entry name="DefaultTheme" type="String">
      <label>Default display theme for new documents.</label>
      <default>default</default>
    </entry>
    <entry name="RememberPerFileSettings" type="Bool">
      <default>true</default>
    </entry>
    <entry name="MetaInfoExpiryDays" type="Int">
      <default>90</default>
      <min>0</min><max>3650</max>
    </entry>
    <entry name="AutoReloadOnChange" type="Bool">
      <default>true</default>
    </entry>
  </group>

  <group name="Display">
    <entry name="DefaultPageSizeName" type="String">
      <label>Default page size (e.g. A4, Letter).</label>
      <default>A4</default>
    </entry>
    <entry name="DefaultZoom" type="Double">
      <default>1.0</default>
      <min>0.25</min><max>4.0</max>
    </entry>
    <entry name="ViewMode" type="Enum">
      <choices>
        <choice name="Continuous"/>
        <choice name="SinglePage"/>
      </choices>
      <default>Continuous</default>
    </entry>
  </group>

  <group name="Rendering">
    <entry name="SyntaxHighlightingEnabled" type="Bool">
      <default>true</default>
    </entry>
    <entry name="CodeHighlightTheme" type="String">
      <label>KSyntaxHighlighting theme name for code blocks.</label>
      <default></default>  <!-- empty = auto from document theme -->
    </entry>
    <entry name="RenderImages" type="Bool">
      <default>true</default>
    </entry>
  </group>
</kcfg>
```

CMake integration:
```cmake
kconfig_add_kcfg_files(KCFG_SRCS app/penelopesettings.kcfgc GENERATE_MOC)
```

Generated class: `PenelopeSettings::self()->defaultTheme()`, etc.

---

## Theme JSON Schema

Each theme defines properties for every named style. Properties not specified
inherit from the parent style (BodyText for paragraphs, DefaultText for characters).

```json
{
    "name": "Academic",
    "version": 1,
    "description": "Clean serif typography for academic reading",
    "author": "Penelope",

    "page": {
        "size": "A4",
        "marginTop": 25.0,
        "marginBottom": 25.0,
        "marginLeft": 30.0,
        "marginRight": 25.0,
        "orientation": "portrait"
    },

    "codeTheme": "GitHub Light",

    "paragraphStyles": {
        "BodyText": {
            "fontFamily": "Noto Serif",
            "fontSize": 11.0,
            "lineHeightPercent": 150,
            "spaceAfter": 6.0,
            "alignment": "justify"
        },
        "Heading1": {
            "fontFamily": "Noto Sans",
            "fontSize": 28.0,
            "fontWeight": "bold",
            "spaceBefore": 24.0,
            "spaceAfter": 12.0,
            "foreground": "#1a1a2e"
        },
        "Heading2": {
            "fontSize": 22.0,
            "fontWeight": "bold",
            "spaceBefore": 20.0,
            "spaceAfter": 10.0
        },
        "Heading3": {
            "fontSize": 18.0,
            "fontWeight": "bold",
            "spaceBefore": 16.0,
            "spaceAfter": 8.0
        },
        "Heading4": {
            "fontSize": 15.0,
            "fontWeight": "bold",
            "spaceBefore": 12.0,
            "spaceAfter": 6.0
        },
        "Heading5": {
            "fontSize": 13.0,
            "fontWeight": "bold",
            "spaceBefore": 10.0,
            "spaceAfter": 4.0
        },
        "Heading6": {
            "fontSize": 11.0,
            "fontWeight": "bold",
            "fontItalic": true,
            "spaceBefore": 8.0,
            "spaceAfter": 4.0
        },
        "BlockQuote": {
            "fontItalic": true,
            "leftMarginPerLevel": 20.0,
            "leftBorderWidth": 3.0,
            "leftBorderColor": "#cccccc",
            "foreground": "#555555"
        },
        "CodeBlock": {
            "fontFamily": "JetBrains Mono, Source Code Pro, monospace",
            "fontSize": 10.0,
            "background": "#f6f8fa",
            "borderWidth": 1.0,
            "borderColor": "#e1e4e8",
            "padding": 12.0,
            "lineHeightPercent": 140
        },
        "ListItemBullet": {
            "indentPerLevel": 24.0,
            "bulletStyle": "disc"
        },
        "ListItemOrdered": {
            "indentPerLevel": 24.0,
            "numberSuffix": "."
        },
        "HorizontalRule": {
            "spaceBefore": 12.0,
            "spaceAfter": 12.0,
            "ruleColor": "#cccccc"
        },
        "TableHeader": {
            "fontWeight": "bold",
            "background": "#f0f0f0",
            "alignment": "center",
            "cellPadding": 6.0,
            "borderWidth": 1.0,
            "borderColor": "#dddddd"
        },
        "TableBody": {
            "cellPadding": 6.0,
            "borderWidth": 1.0,
            "borderColor": "#dddddd"
        }
    },

    "characterStyles": {
        "DefaultText": {
            "fontFamily": "Noto Serif",
            "fontSize": 11.0,
            "foreground": "#1a1a1a"
        },
        "Emphasis": {
            "fontItalic": true
        },
        "Strong": {
            "fontWeight": "bold"
        },
        "StrongEmphasis": {
            "fontWeight": "bold",
            "fontItalic": true
        },
        "InlineCode": {
            "fontFamily": "JetBrains Mono, Source Code Pro, monospace",
            "fontSize": 10.0,
            "foreground": "#c7254e",
            "background": "#f0f0f0"
        },
        "Link": {
            "foreground": "#0366d6",
            "underline": true
        },
        "Strikethrough": {
            "strikeOut": true
        },
        "Underline": {
            "underline": true
        }
    }
}
```

### Style Property Types

| Property | Applies To | Type | Notes |
|----------|-----------|------|-------|
| fontFamily | both | string | Comma-separated fallback list |
| fontSize | both | float | Points |
| fontWeight | both | string/int | "normal", "bold", or 100-900 |
| fontItalic | both | bool | |
| foreground | both | string | Hex color |
| background | both | string | Hex color |
| underline | char | bool | |
| strikeOut | char | bool | |
| alignment | para | string | "left", "right", "center", "justify" |
| spaceBefore | para | float | Points |
| spaceAfter | para | float | Points |
| leftMarginPerLevel | para | float | For block quotes |
| lineHeightPercent | para | int | 100 = single, 150 = 1.5x |
| indentPerLevel | para | float | For lists |
| cellPadding | para | float | For table styles |
| borderWidth | para | float | |
| borderColor | para | string | Hex color |
| padding | para | float | For code blocks |

---

## Per-File Metadata JSON

Following Kate's pattern: URL-keyed with content hash prefix for staleness
detection.

```json
{
    "version": 1,
    "files": {
        "file:///home/user/notes/thesis.md": {
            "contentHashPrefix": "a3f2b8c1",
            "lastOpened": "2026-01-31T14:22:00Z",
            "theme": "academic",
            "zoom": 1.15,
            "scrollPosition": 0.42,
            "pageSize": "A4",
            "images": {
                "figures/chart.png": {
                    "width": 400,
                    "height": 300,
                    "alignment": "center",
                    "wrapping": "square"
                }
            }
        },
        "file:///home/user/notes/todo.md": {
            "contentHashPrefix": "7c1e3d9f",
            "lastOpened": "2026-01-30T09:00:00Z",
            "theme": null,
            "zoom": 1.0,
            "scrollPosition": 0.0
        }
    }
}
```

- `theme: null` means use the global default theme.
- `contentHashPrefix` is the first 8 hex chars of SHA-256 of file contents.
  On load, if the prefix doesn't match, some settings (scroll position) are
  discarded while others (theme, zoom) are kept.
- Entries older than `MetaInfoExpiryDays` are purged on startup.

---

## Code Highlight Theme Integration

KSyntaxHighlighting ships with 31 themes (Breeze Light/Dark, Monokai, Dracula,
Solarized, GitHub, Nord, Catppuccin, etc.). The integration:

1. Each document theme has a `codeTheme` field (string matching a
   KSyntaxHighlighting theme name, or empty for auto-detection).
2. When `codeTheme` is empty, Penelope selects the KSyntaxHighlighting
   theme that best matches the document theme's code block background
   (light vs dark).
3. The style dock has a code theme dropdown populated from
   `KSyntaxHighlighting::Repository::themes()`.
4. Changing the code theme re-runs the highlighting pass without
   re-parsing the markdown.

### Highlighting Implementation (AbstractHighlighter Subclass)

We subclass `KSyntaxHighlighting::AbstractHighlighter` rather than using
`SyntaxHighlighter` (which would highlight the entire document). Our subclass
collects format ranges, then applies them via `QTextCursor::mergeCharFormat()`
only to code block regions.

```cpp
class CodeBlockHighlighter : public KSyntaxHighlighting::AbstractHighlighter {
    // highlightLine() collects FormatRange structs
    // applyFormat() converts KSyntaxHighlighting::Format -> QTextCharFormat
    // Applied per-block via QTextCursor after document construction
};
```

Each code block can use a different language `Definition` based on its
`BlockCodeLanguage` property. The `State` is chained within a single code block
and reset at each new fence.

---

## Style Dock Panel Design

The style dock is a `QDockWidget` on the right side of the main window,
containing a scrollable panel with these sections:

```
Style Dock (QDockWidget)
└── QScrollArea
    └── QWidget (content)
        ├── Theme Section
        │   ├── QComboBox (theme preset selector)
        │   ├── QPushButton "Save as New Theme..."
        │   └── QPushButton "Reset to Theme Defaults"
        │
        ├── Page Section (collapsible)
        │   ├── QComboBox (page size: A4, Letter, Legal, Custom)
        │   ├── Orientation toggle (Portrait / Landscape)
        │   └── Margin spinboxes (top, bottom, left, right)
        │
        ├── Typography Section (collapsible)
        │   ├── QTreeWidget or QListWidget (style list)
        │   │   ├── "Body Text" -> expands to show:
        │   │   │   ├── Font family (QFontComboBox)
        │   │   │   ├── Font size (QDoubleSpinBox)
        │   │   │   ├── Line spacing (QComboBox: single/1.5/double/custom)
        │   │   │   ├── Paragraph spacing before/after (QDoubleSpinBox)
        │   │   │   └── Alignment (QButtonGroup: L/C/R/J)
        │   │   ├── "Heading 1" -> same controls
        │   │   ├── "Heading 2" -> same controls
        │   │   ├── ... (all paragraph styles)
        │   │   ├── "Inline Code" -> char style controls
        │   │   ├── "Link" -> color picker, underline toggle
        │   │   └── "Block Quote" -> border color, italic toggle
        │   └── (each item expandable inline or via a properties pane below)
        │
        ├── Code Highlighting Section (collapsible)
        │   ├── QComboBox (KSyntaxHighlighting theme selector)
        │   └── Preview swatch showing sample highlighted code
        │
        └── Colors Section (collapsible)
            ├── Foreground color (KColorButton)
            ├── Background color (KColorButton)
            └── Link color (KColorButton)
```

### Live Preview

All changes in the dock immediately re-apply styles to the document and trigger
a repaint. The flow is:

1. User changes a property in the dock
2. ThemeManager updates the in-memory theme
3. KoStyleManager re-applies styles to all blocks/fragments
4. Layout engine re-paginates
5. Canvas repaints visible pages

For performance, style changes batch via a short debounce timer (100ms) so
rapid slider/spinbox changes don't cause excessive relayout.

### Theme Presets

- Built-in themes are read-only JSON files bundled as Qt resources.
- "Save as New Theme" copies the current state to a new user JSON file.
- User themes are written to `~/.local/share/Penelope/themes/`.
- The theme selector dropdown shows built-in themes first, then a separator,
  then user themes.

---

## File Watching

When `AutoReloadOnChange` is enabled, Penelope watches the open `.md` file
using `QFileSystemWatcher` + debounce timer (300ms). On change:

1. Re-read file contents
2. Check content hash prefix against stored metadata
3. Re-run `DocumentBuilder::build()` with new content
4. Re-apply code highlighting
5. Attempt to preserve scroll position (by finding the nearest heading
   or paragraph that still exists)

The parent directory is also watched to handle the delete-and-recreate pattern
used by many editors (Vim, Emacs, Kate) when saving files.
