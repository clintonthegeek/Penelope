#ifndef PENELOPE_CODEBLOCKHIGHLIGHTER_H
#define PENELOPE_CODEBLOCKHIGHLIGHTER_H

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QTextBlock>
#include <QTextDocument>

class CodeBlockHighlighter : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    CodeBlockHighlighter();

    void highlight(QTextDocument *document);

protected:
    void applyFormat(int offset, int length,
                     const KSyntaxHighlighting::Format &format) override;

private:
    KSyntaxHighlighting::Repository m_repository;
    QTextBlock m_currentBlock;
};

#endif // PENELOPE_CODEBLOCKHIGHLIGHTER_H
