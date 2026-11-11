// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <optional>

#include <ghostty/vt.h>

#include "terminalsession.h"
#include "theme.h"

// Draws a TerminalSession and turns touch, keys and input-method text
// into terminal input. Rows are painted on the GUI thread (only the rows
// libghostty reports dirty) and handed to the scene graph as one texture
// per row.
class TerminalItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(TerminalSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY fontChanged)
    Q_PROPERTY(QString lightTheme READ lightTheme WRITE setLightTheme NOTIFY themeChanged)
    Q_PROPERTY(QString darkTheme READ darkTheme WRITE setDarkTheme NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY themeChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(QColor foregroundColor READ foregroundColor NOTIFY themeChanged)
    Q_PROPERTY(qreal cellWidth READ cellWidth NOTIFY metricsChanged)
    Q_PROPERTY(qreal cellHeight READ cellHeight NOTIFY metricsChanged)
    Q_PROPERTY(bool ctrlLatched READ ctrlLatched WRITE setCtrlLatched NOTIFY latchChanged)
    Q_PROPERTY(bool altLatched READ altLatched WRITE setAltLatched NOTIFY latchChanged)
    Q_PROPERTY(QString preedit READ preedit NOTIFY preeditChanged)
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectingChanged)
    Q_PROPERTY(QRectF selectionStartRect READ selectionStartRect NOTIFY selectionGeometryChanged)
    Q_PROPERTY(QRectF selectionEndRect READ selectionEndRect NOTIFY selectionGeometryChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);
    ~TerminalItem() override;

    TerminalSession *session() const { return m_session; }
    void setSession(TerminalSession *s);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &f);
    qreal fontSize() const { return m_fontSize; }
    void setFontSize(qreal pt);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal f);
    QString lightTheme() const { return m_lightThemeName; }
    void setLightTheme(const QString &n);
    QString darkTheme() const { return m_darkThemeName; }
    void setDarkTheme(const QString &n);
    bool dark() const { return m_dark; }
    void setDark(bool d);
    QColor backgroundColor() const { return m_theme.background; }
    QColor foregroundColor() const { return m_theme.foreground; }
    qreal cellWidth() const { return m_cellW / m_dpr; }
    qreal cellHeight() const { return m_cellH / m_dpr; }
    bool ctrlLatched() const { return m_ctrlLatched; }
    void setCtrlLatched(bool v);
    bool altLatched() const { return m_altLatched; }
    void setAltLatched(bool v);
    QString preedit() const { return m_preedit; }
    bool selecting() const { return m_selectMode; }
    QRectF selectionStartRect() const;
    QRectF selectionEndRect() const;

    // Extra-keys row: "Esc", "Tab", "Up", "PgUp", "F5", ... or literal text.
    Q_INVOKABLE void sendKeyName(const QString &name);
    Q_INVOKABLE void sendString(const QString &text);
    Q_INVOKABLE void showKeyboard();
    Q_INVOKABLE void zoom(qreal factor);
    // Selection handles in QML call these with item coordinates.
    Q_INVOKABLE void moveSelectionEdge(bool start, qreal x, qreal y);
    Q_INVOKABLE void endSelectionMode();
    // What a long press does: select the word there and show the handles.
    Q_INVOKABLE void selectWordAtPoint(qreal x, qreal y) { beginTouchSelection(QPointF(x, y)); }

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

Q_SIGNALS:
    void sessionChanged();
    void fontChanged();
    void themeChanged();
    void metricsChanged();
    void latchChanged();
    void preeditChanged();
    void selectingChanged();
    void selectionGeometryChanged();
    void linkActivated(const QString &url);
    void contextMenuRequested(qreal x, qreal y);
    void tapped();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void updatePolish() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void releaseResources() override;

    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void touchEvent(QTouchEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void timerEvent(QTimerEvent *e) override;

private:
    struct Fonts {
        // Spaced to the cell width, for ASCII runs.
        QFont regular, bold, italic, boldItalic;
        // Unspaced, for single cells (CJK, symbols, emoji), with metrics.
        QFont plain[4];
        std::optional<QFontMetricsF> plainMetrics[4];
        int baseline = 0;
        int strikeY = 0;
        static int index(bool b, bool i) { return (b ? 1 : 0) | (i ? 2 : 0); }
        const QFont &pick(bool b, bool i) const { return b ? (i ? boldItalic : bold) : (i ? italic : regular); }
    };

    void updateMetrics();
    void relayout();
    void applyTheme();
    void scheduleRender(bool full = false);
    void renderRow(int y, GhosttyRenderStateRowIterator rows, const GhosttyRenderStateColors &colors);
    void paintCursor(QPainter &p, int y, const GhosttyRenderStateColors &colors);
    QPoint cellAt(QPointF itemPos) const;
    QPointF cellToItem(QPoint cell) const;
    GhosttyMods currentMods(Qt::KeyboardModifiers m) const;
    void consumeLatches();
    void scrollByPixels(qreal dy);
    void beginTouchSelection(QPointF pos);
    void handleTap(QPointF pos);
    void startFling(qreal velocity);
    void sendMouseButton(QPointF pos, GhosttyMouseAction action, GhosttyMouseButton button, Qt::KeyboardModifiers mods);

    QPointer<TerminalSession> m_session;
    QMetaObject::Connection m_contentConn;
    GhosttyRenderState m_renderState = nullptr;
    GhosttyRenderStateRowIterator m_rowIter = nullptr;
    GhosttyRenderStateRowCells m_cells = nullptr;

    QString m_fontFamily;
    qreal m_fontSize = 11;
    qreal m_lineHeight = 1.0;
    QString m_lightThemeName = QStringLiteral("GitHub Light Default");
    QString m_darkThemeName = QStringLiteral("Breeze");
    bool m_dark = false;
    Theme m_theme = Theme::builtinLight();

    Fonts m_fonts;
    qreal m_dpr = 1;
    int m_cellW = 10; // device pixels
    int m_cellH = 20;
    int m_padX = 0; // device pixels
    int m_padY = 0;
    int m_cols = 0;
    int m_rows = 0;

    std::vector<QImage> m_rowImages;
    std::vector<bool> m_rowUpload;
    bool m_fullRender = true;
    bool m_geometryDirty = true;
    int m_lastCursorRow = -1;
    QColor m_bgForNode;

    bool m_ctrlLatched = false;
    bool m_altLatched = false;
    QString m_preedit;
    bool m_focused = false;
    bool m_cursorBlinkOn = true;
    QBasicTimer m_blinkTimer;
    QElapsedTimer m_lastActivity;

    // Touch state.
    enum class Gesture { None, Pending, Scroll, Select, Pinch, MouseDrag };
    Gesture m_gesture = Gesture::None;
    QPointF m_touchStart;
    QPointF m_touchLast;
    QPoint m_selAnchor;
    QPoint m_selHead;
    bool m_selectMode = false;
    QBasicTimer m_longPressTimer;
    qreal m_scrollAccum = 0;
    QElapsedTimer m_moveClock;
    qreal m_velocity = 0;
    QBasicTimer m_flingTimer;
    QElapsedTimer m_flingClock;
    qreal m_pinchStartDist = 0;
    qreal m_pinchScale = 1;
    qreal m_pinchStartFont = 11;
    bool m_mouseSelecting = false;
    QPoint m_lastMouseCell{-1, -1};
};
