#include "codeblockhighlighter.h"
#include "penelopesettings.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/State>

#include <QFont>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>

CodeBlockHighlighter::CodeBlockHighlighter()
{
    KSyntaxHighlighting::Theme defaultTheme =
        m_repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
    auto chosen = m_repository.theme(PenelopeSettings::self()->codeHighlightTheme());
    AbstractHighlighter::setTheme(chosen.isValid() ? chosen : defaultTheme);
}

void CodeBlockHighlighter::highlight(QTextDocument *document)
{
    if (!PenelopeSettings::self()->syntaxHighlightingEnabled()) return;
    QTextBlock block = document->begin();
    KSyntaxHighlighting::State state;
    QString currentLang;

    while (block.isValid()) {
        QTextBlockFormat bf = block.blockFormat();
        QString lang = bf.property(QTextFormat::BlockCodeLanguage).toString();

        if (!lang.isEmpty()) {
            if (lang != currentLang) {
                currentLang = lang;
                auto def = m_repository.definitionForName(lang);
                if (!def.isValid())
                    def = m_repository.definitionForFileName(
                        QStringLiteral("file.") + lang);
                setDefinition(def);
                state = KSyntaxHighlighting::State();
            }

            if (definition().isValid()) {
                m_currentBlock = block;
                state = highlightLine(block.text(), state);
            }
        } else {
            if (!currentLang.isEmpty()) {
                currentLang.clear();
                state = KSyntaxHighlighting::State();
            }
        }

        block = block.next();
    }
}

void CodeBlockHighlighter::applyFormat(
    int offset, int length,
    const KSyntaxHighlighting::Format &format)
{
    if (!m_currentBlock.isValid())
        return;
    if (format.isDefaultTextStyle(theme()))
        return;

    QTextCursor cursor(m_currentBlock);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, offset);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, length);

    QTextCharFormat cf;
    if (format.hasTextColor(theme()))
        cf.setForeground(format.textColor(theme()));
    if (format.hasBackgroundColor(theme()))
        cf.setBackground(format.backgroundColor(theme()));
    if (format.isBold(theme()))
        cf.setFontWeight(QFont::Bold);
    if (format.isItalic(theme()))
        cf.setFontItalic(true);
    if (format.isUnderline(theme()))
        cf.setFontUnderline(true);
    if (format.isStrikeThrough(theme()))
        cf.setFontStrikeOut(true);

    cursor.mergeCharFormat(cf);
}
