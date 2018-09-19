// Internal
#include <internal/QLineNumberArea.hpp>
#include <QSyntaxStyle>
#include <QCodeEditor>
#include <QStyleSyntaxHighlighter>

// Qt
#include <QTextBlock>
#include <QPaintEvent>
#include <QFontDatabase>
#include <QScrollBar>
#include <QAbstractTextDocumentLayout>
#include <QTextCharFormat>
#include <internal/QCodeEditor.hpp>
#include <QCursor>

#include <QDebug>

static QVector<QPair<QString, QString>> parentheses = {
    {"(", ")"},
    {"{", "}"},
    {"<", ">"},
    {"[", "]"},
    {"\"", "\""}
};

QCodeEditor::QCodeEditor(QWidget* widget) :
    QTextEdit(widget),
    m_highlighter(nullptr),
    m_syntaxStyle(nullptr),
    m_lineNumberArea(nullptr),
    m_autoIndentation(true),
    m_autoParentheses(true),
    m_replaceTab(true),
    m_tabReplace(QString(4, ' '))
{
    auto fnt = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fnt.setFixedPitch(true);
    fnt.setPointSize(10);

    setFont(fnt);

    m_lineNumberArea = new QLineNumberArea(this);

    connect(
        document(),
        &QTextDocument::blockCountChanged,
        this,
        &QCodeEditor::updateLineNumberAreaWidth
    );

    connect(
        verticalScrollBar(),
        &QScrollBar::valueChanged,
        [this](int){ m_lineNumberArea->update(); }
    );

    connect(
        this,
        &QTextEdit::cursorPositionChanged,
        this,
        &QCodeEditor::updateExtraSelection
    );

    setSyntaxStyle(QSyntaxStyle::defaultStyle());
}

void QCodeEditor::setHighlighter(QStyleSyntaxHighlighter* highlighter)
{
    m_highlighter = highlighter;

    if (m_highlighter)
    {
        m_highlighter->setSyntaxStyle(m_syntaxStyle);
        m_highlighter->setDocument(document());
    }
}

void QCodeEditor::setSyntaxStyle(QSyntaxStyle* style)
{
    m_syntaxStyle = style;

    m_lineNumberArea->setSyntaxStyle(m_syntaxStyle);

    if (m_highlighter)
    {
        m_highlighter->setSyntaxStyle(m_syntaxStyle);
    }

    updateStyle();
}

void QCodeEditor::updateStyle()
{
    if (m_highlighter)
    {
        m_highlighter->rehighlight();
    }

    if (m_syntaxStyle)
    {
        auto currentPalette = palette();

        // Setting text format/color
        currentPalette.setColor(
            QPalette::ColorRole::Text,
            m_syntaxStyle->getFormat("Text").foreground().color()
        );

        // Setting common background
        currentPalette.setColor(
            QPalette::Base,
            m_syntaxStyle->getFormat("Text").background().color()
        );

        // Setting selection color
        currentPalette.setColor(
            QPalette::Highlight,
            m_syntaxStyle->getFormat("Selection").background().color()
        );

        setPalette(currentPalette);
    }
}

void QCodeEditor::resizeEvent(QResizeEvent* e)
{
    QTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), m_lineNumberArea->sizeHint().width(), cr.height()));
}

void QCodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(m_lineNumberArea->sizeHint().width(), 0, 0, 0);
}

void QCodeEditor::updateLineNumberArea(const QRect& rect)
{
    m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void QCodeEditor::updateExtraSelection()
{
    QList<QTextEdit::ExtraSelection> extra;

    highlightCurrentLine(extra);
    highlightParenthesis(extra);

    setExtraSelections(extra);
}

void QCodeEditor::highlightParenthesis(QList<QTextEdit::ExtraSelection>& extraSelection)
{
    auto currentSymbol = getCharUnderCursor();
    auto prevSymbol = getCharUnderCursor(-1);

    for (auto& pair : parentheses)
    {
        int direction;

        QChar counterSymbol;
        QChar activeSymbol;
        auto position = textCursor().position();

        if (pair.first == currentSymbol)
        {
            direction = 1;
            counterSymbol = pair.second[0];
            activeSymbol = currentSymbol;
        }
        else if (pair.second == prevSymbol)
        {
            direction = -1;
            counterSymbol = pair.first[0];
            activeSymbol = prevSymbol;
            position--;
        }
        else
        {
            continue;
        }

        auto counter = 1;

        while (counter != 0 &&
               position > 0 &&
               position < (document()->characterCount() - 1))
        {
            // Moving position
            position += direction;

            auto character = document()->characterAt(position);
            // Checking symbol under position
            if (character == activeSymbol)
            {
                ++counter;
            }
            else if (character == counterSymbol)
            {
                --counter;
            }
        }

        auto format = m_syntaxStyle->getFormat("Parenthesis");

        // Found
        if (counter == 0)
        {
            ExtraSelection selection{};

            auto directionEnum =
                 direction < 0 ?
                 QTextCursor::MoveOperation::Left
                 :
                 QTextCursor::MoveOperation::Right;

            selection.format = format;
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selection.cursor.movePosition(
                directionEnum,
                QTextCursor::MoveMode::MoveAnchor,
                std::abs(textCursor().position() - position)
            );

            selection.cursor.movePosition(
                QTextCursor::MoveOperation::Right,
                QTextCursor::MoveMode::KeepAnchor,
                1
            );

            extraSelection.append(selection);

            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selection.cursor.movePosition(
                directionEnum,
                QTextCursor::MoveMode::KeepAnchor,
                1
            );

            extraSelection.append(selection);
        }

        break;
    }
}

void QCodeEditor::highlightCurrentLine(QList<QTextEdit::ExtraSelection>& extraSelection)
{
    if (!isReadOnly())
    {
        QTextEdit::ExtraSelection selection{};

        selection.format = m_syntaxStyle->getFormat("CurrentLine");
        selection.format.setForeground(QBrush());
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();

        extraSelection.append(selection);
    }
}

void QCodeEditor::paintEvent(QPaintEvent* e)
{
    updateLineNumberArea(e->rect());
    QTextEdit::paintEvent(e);
}

int QCodeEditor::getFirstVisibleBlock()
{
    // Detect the first block for which bounding rect - once translated
    // in absolute coordinated - is contained by the editor's text area

    // Costly way of doing but since "blockBoundingGeometry(...)" doesn't
    // exists for "QTextEdit"...

    QTextCursor curs = QTextCursor(document());
    curs.movePosition(QTextCursor::Start);
    for(int i=0; i < document()->blockCount(); ++i)
    {
        QTextBlock block = curs.block();

        QRect r1 = viewport()->geometry();
        QRect r2 = document()
            ->documentLayout()
            ->blockBoundingRect(block)
            .translated(
                viewport()->geometry().x(),
                viewport()->geometry().y() - verticalScrollBar()->sliderPosition()
            ).toRect();

        if (r1.intersects(r2))
        {
            return i;
        }

        curs.movePosition(QTextCursor::NextBlock);
    }

    return 0;
}

void QCodeEditor::keyPressEvent(QKeyEvent* e)
{
    if (m_replaceTab && e->key() == Qt::Key_Tab)
    {
        e->ignore();
        insertPlainText(m_tabReplace);
        return;
    }

    // Auto indentation
    int indentationLevel = 0;
    if (m_autoIndentation && e->key() == Qt::Key_Return)
    {
        auto blockText = textCursor().block().text();

        for (auto i = 0;
             i < blockText.size() && QString("\t ").contains(blockText[i]);
             ++i)
        {
            if (blockText[i] == ' ')
            {
                indentationLevel++;
            }
            else
            {
                indentationLevel += tabStopWidth() / fontMetrics().averageCharWidth();
            }
        }
    }

    QTextEdit::keyPressEvent(e);

    if (m_autoIndentation && e->key() == Qt::Key_Return)
    {
        insertPlainText(QString(indentationLevel, ' '));
    }

    if (m_autoParentheses)
    {
        for (auto&& el : parentheses)
        {
            // Inserting closed brace
            if (el.first == e->text())
            {
                insertPlainText(el.second);
                moveCursor(QTextCursor::MoveOperation::Left);
                break;
            }

            // If it's close brace - check parentheses
            if (el.second == e->text())
            {
                auto symbol = getCharUnderCursor();

                if (symbol == el.second)
                {
                    textCursor().deletePreviousChar();
                    moveCursor(QTextCursor::MoveOperation::Right);
                }

                break;
            }
        }
    }
}

void QCodeEditor::setAutoParentheses(bool enabled)
{
    m_autoParentheses = enabled;
}

bool QCodeEditor::autoParentheses() const
{
    return m_autoParentheses;
}

void QCodeEditor::setTabReplace(bool enabled)
{
    m_replaceTab = enabled;
}

bool QCodeEditor::tabReplace() const
{
    return m_replaceTab;
}

void QCodeEditor::setTabReplaceSize(int val)
{
    m_tabReplace.clear();

    m_tabReplace.fill(' ', val);
}

int QCodeEditor::tabReplaceSize() const
{
    return m_tabReplace.size();
}

QChar QCodeEditor::getCharUnderCursor(int offset) const
{

    auto block = textCursor().blockNumber();
    auto index = textCursor().positionInBlock();
    auto text = document()->findBlockByNumber(block).text();

    index += offset;

    if (index < 0 || index >= text.size())
    {
        return QChar();
    }

    return text[index];
}