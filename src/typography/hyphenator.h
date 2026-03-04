#ifndef PENELOPE_HYPHENATOR_H
#define PENELOPE_HYPHENATOR_H

#include <QHash>
#include <QString>

// Opaque forward declaration matching the typedef in hyphen.h:
//   typedef struct _HyphenDict HyphenDict;
// We use the struct tag directly to avoid redeclaration conflicts.
struct _HyphenDict;

class Hyphenator
{
public:
    Hyphenator();
    ~Hyphenator();

    bool loadDictionary(const QString &language);
    bool isLoaded() const { return m_dict != nullptr; }

    // Insert soft hyphens (U+00AD) at valid break points in a word.
    // Returns the word unchanged if no hyphenation points are found
    // or the word is shorter than m_minWordLength.
    QString hyphenate(const QString &word) const;

    // Process a full text string, hyphenating words while preserving
    // whitespace, punctuation, and existing hyphens.
    QString hyphenateText(const QString &text) const;

    // Available dictionary languages (scans resource paths)
    static QStringList availableLanguages();

    // Set minimum word length for hyphenation
    void setMinWordLength(int len) { m_minWordLength = len; }

private:
    _HyphenDict *m_dict = nullptr;
    int m_minWordLength = 5;
    QString m_language;

    static QHash<QString, QString> s_dictPaths;
    static void initDictPaths();
};

#endif // PENELOPE_HYPHENATOR_H
