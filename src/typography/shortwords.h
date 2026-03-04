#ifndef PENELOPE_SHORTWORDS_H
#define PENELOPE_SHORTWORDS_H

#include <QSet>
#include <QString>
#include <QStringList>

class ShortWords
{
public:
    ShortWords();

    // Load a language-specific word list. Falls back to English if not found.
    void setLanguage(const QString &language);

    // Process text: replace spaces after short words with non-breaking spaces (U+00A0).
    // Prevents short prepositions, articles, and conjunctions from being stranded
    // at line ends.
    QString process(const QString &text) const;

private:
    void loadEnglish();
    void loadCzech();
    void loadPolish();
    void loadFrench();
    void loadGerman();

    QSet<QString> m_words;
    QString m_language;
};

#endif // PENELOPE_SHORTWORDS_H
