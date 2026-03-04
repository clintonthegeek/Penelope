#include "hyphenator.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <hyphen.h>

QHash<QString, QString> Hyphenator::s_dictPaths;

static constexpr QChar kSoftHyphen(0x00AD);

Hyphenator::Hyphenator() = default;

Hyphenator::~Hyphenator()
{
    if (m_dict)
        hnj_hyphen_free(m_dict);
}

void Hyphenator::initDictPaths()
{
    if (!s_dictPaths.isEmpty())
        return;

    // 1. Bundled dictionaries in Qt resources
    QDir resourceDir(QStringLiteral(":/dicts"));
    if (resourceDir.exists()) {
        const QStringList entries = resourceDir.entryList(
            {QStringLiteral("hyph_*.dic")}, QDir::Files);
        for (const QString &entry : entries) {
            // Extract language code: hyph_en_US.dic -> en_US
            QString lang = entry.mid(5, entry.length() - 9);
            s_dictPaths.insert(lang, resourceDir.filePath(entry));
        }
    }

    // 2. System dictionaries (common paths)
    static const char *systemPaths[] = {
        "/usr/share/hyphen",
        "/usr/share/hunspell",
        "/usr/share/myspell/dicts",
        "/usr/local/share/hyphen",
    };

    for (const char *path : systemPaths) {
        QDir dir(QString::fromLatin1(path));
        if (!dir.exists())
            continue;
        const QStringList entries = dir.entryList(
            {QStringLiteral("hyph_*.dic")}, QDir::Files);
        for (const QString &entry : entries) {
            QString lang = entry.mid(5, entry.length() - 9);
            if (!s_dictPaths.contains(lang))
                s_dictPaths.insert(lang, dir.filePath(entry));
        }
    }
}

QStringList Hyphenator::availableLanguages()
{
    initDictPaths();
    QStringList langs = s_dictPaths.keys();
    langs.sort();
    return langs;
}

bool Hyphenator::loadDictionary(const QString &language)
{
    if (m_dict) {
        hnj_hyphen_free(m_dict);
        m_dict = nullptr;
        m_language.clear();
    }

    initDictPaths();

    QString path = s_dictPaths.value(language);
    if (path.isEmpty()) {
        // Try base language (e.g., "en" -> "en_US")
        for (auto it = s_dictPaths.constBegin(); it != s_dictPaths.constEnd(); ++it) {
            if (it.key().startsWith(language)) {
                path = it.value();
                break;
            }
        }
    }

    if (path.isEmpty())
        return false;

    // libhyphen needs a real file path, not a Qt resource path.
    // If the dictionary is embedded as a resource, extract to a temp file.
    QString realPath = path;
    if (path.startsWith(QLatin1String(":/"))) {
        QFile resource(path);
        if (!resource.open(QIODevice::ReadOnly))
            return false;

        // Write to a persistent temp file (lives as long as the app)
        QString tempDir = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
        realPath = tempDir + QLatin1String("/penelope_")
                   + QFileInfo(path).fileName();
        QFile tempFile(realPath);
        if (!tempFile.open(QIODevice::WriteOnly))
            return false;
        tempFile.write(resource.readAll());
        tempFile.close();
    }

    m_dict = hnj_hyphen_load(realPath.toLocal8Bit().constData());
    if (m_dict) {
        m_language = language;
        return true;
    }
    return false;
}

QString Hyphenator::hyphenate(const QString &word) const
{
    if (!m_dict || word.length() < m_minWordLength)
        return word;

    QByteArray utf8 = word.toUtf8();
    int wordLen = utf8.length();

    // libhyphen output buffer
    QByteArray hyphens(wordLen + 5, 0);
    char **rep = nullptr;
    int *pos = nullptr;
    int *cut = nullptr;

    int ret = hnj_hyphen_hyphenate2(
        m_dict, utf8.constData(), wordLen,
        hyphens.data(), nullptr, &rep, &pos, &cut);

    if (ret != 0)
        return word;

    // Build result with soft hyphens at odd-numbered positions.
    // The hyphens array contains '0'-'9' characters; odd digits
    // indicate valid break points.
    QString result;
    result.reserve(word.length() + 10);

    // Map UTF-8 byte positions back to QString character positions.
    // Build a byte-offset -> char-index map.
    QVector<int> byteToChar(wordLen + 1, 0);
    int bytePos = 0;
    for (int charIdx = 0; charIdx < word.length(); ++charIdx) {
        byteToChar[bytePos] = charIdx;
        QChar ch = word[charIdx];
        if (ch.unicode() < 0x80) bytePos += 1;
        else if (ch.unicode() < 0x800) bytePos += 2;
        else if (ch.isHighSurrogate()) { bytePos += 4; ++charIdx; }
        else bytePos += 3;
    }

    // Track position in the original word independently of result length,
    // since soft hyphens inserted into result inflate its length.
    int wordPos = 0;
    int prevCharIdx = 0;
    for (int i = 0; i < wordLen; ++i) {
        int charIdx = (i < byteToChar.size()) ? byteToChar[i] : prevCharIdx;
        prevCharIdx = charIdx;

        if ((hyphens[i] - '0') & 1) {
            // Valid break point after this byte position
            // Only insert if we're past minimum prefix (2 chars) and
            // before minimum suffix (2 chars from end)
            int charAfter = (i + 1 < byteToChar.size()) ? byteToChar[i + 1] : word.length();
            if (charAfter >= 2 && charAfter <= word.length() - 2) {
                // Copy characters from wordPos up to charAfter
                while (wordPos < charAfter)
                    result.append(word[wordPos++]);
                result.append(kSoftHyphen);
            }
        }
    }

    // Append remaining characters
    while (wordPos < word.length())
        result.append(word[wordPos++]);

    // Free allocated memory from hnj_hyphen_hyphenate2
    if (rep) {
        for (int i = 0; i < wordLen; ++i)
            free(rep[i]);
        free(rep);
    }
    free(pos);
    free(cut);

    return result;
}

QString Hyphenator::hyphenateText(const QString &text) const
{
    if (!m_dict || text.isEmpty())
        return text;

    // Split text into words and non-words, preserving everything
    static const QRegularExpression wordRx(
        QStringLiteral(R"([\p{L}\p{M}]+)"));

    QString result;
    result.reserve(text.length() + text.length() / 10);

    int lastEnd = 0;
    auto it = wordRx.globalMatch(text);
    while (it.hasNext()) {
        auto match = it.next();
        // Append non-word text before this word
        if (match.capturedStart() > lastEnd)
            result.append(text.mid(lastEnd, match.capturedStart() - lastEnd));

        // Hyphenate the word
        result.append(hyphenate(match.captured()));
        lastEnd = match.capturedEnd();
    }

    // Append remaining non-word text
    if (lastEnd < text.length())
        result.append(text.mid(lastEnd));

    return result;
}
