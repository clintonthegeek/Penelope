#ifndef PENELOPE_FRONTMATTERPARSER_H
#define PENELOPE_FRONTMATTERPARSER_H

#include <QString>

// YAML front matter, extracted before MD4C sees the text.
//
// The rule mirrors markoff-parser so both readers agree on where the body
// starts (Markoff/libs/markoff-parser/src/Document.cpp, Document::extract):
// the source must begin with "---\n" (or CRLF), the block runs to the next
// "\n---", and the body begins after that line. Without a closing line there
// is no front matter, except when the file itself ends with "\n---".
struct FrontMatter
{
    bool present = false;
    QString raw; // YAML body, delimiters stripped
};

class FrontMatterParser
{
public:
    FrontMatterParser() = default;

    // Returns the markdown with the front-matter lines blanked. Line count and
    // line numbering are preserved, the way FootnoteParser blanks definitions,
    // so source ranges keep addressing the original file.
    QString process(const QString &markdownText);

    const FrontMatter &frontMatter() const { return m_frontMatter; }

private:
    FrontMatter m_frontMatter;
};

#endif // PENELOPE_FRONTMATTERPARSER_H
