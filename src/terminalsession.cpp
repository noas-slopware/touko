// SPDX-License-Identifier: GPL-2.0-or-later
#include "terminalsession.h"

#include "config.h"
#include "ptyprocess.h"
#include "shellenv.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <cstring>

namespace {

GhosttyColorRgb rgb(const QColor &c)
{
    return GhosttyColorRgb{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue())};
}

QString fromGhostty(GhosttyString s)
{
    if (!s.ptr || !s.len)
        return {};
    return QString::fromUtf8(reinterpret_cast<const char *>(s.ptr), qsizetype(s.len));
}

GhosttyString toGhostty(const QByteArray &b)
{
    return GhosttyString{reinterpret_cast<const uint8_t *>(b.constData()), size_t(b.size())};
}

TerminalSession *self(void *ud)
{
    return static_cast<TerminalSession *>(ud);
}

} // namespace

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent)
{
    ghostty_terminal_new(nullptr, &m_terminal, quint16(m_cols), quint16(m_rows));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_USERDATA, this);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, reinterpret_cast<const void *>(&cbWritePty));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_BELL, reinterpret_cast<const void *>(&cbBell));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, reinterpret_cast<const void *>(&cbTitle));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_PWD_CHANGED, reinterpret_cast<const void *>(&cbPwd));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SIZE, reinterpret_cast<const void *>(&cbSize));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, reinterpret_cast<const void *>(&cbColorScheme));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES,
                         reinterpret_cast<const void *>(&cbDeviceAttributes));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_XTVERSION, reinterpret_cast<const void *>(&cbXtversion));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_WRITE,
                         reinterpret_cast<const void *>(&cbClipboardWrite));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_CLIPBOARD_READ,
                         reinterpret_cast<const void *>(&cbClipboardRead));
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_DESKTOP_NOTIFICATION,
                         reinterpret_cast<const void *>(&cbNotification));
    const size_t scrollbackLines = 10000;
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &scrollbackLines);

    m_xtversion = QByteArrayLiteral(TOUKO_APP_NAME " " TOUKO_VERSION);

    ghostty_key_encoder_new(nullptr, &m_keyEncoder);
    ghostty_key_event_new(nullptr, &m_keyEvent);
    ghostty_mouse_encoder_new(nullptr, &m_mouseEncoder);
    ghostty_mouse_event_new(nullptr, &m_mouseEvent);
}

TerminalSession::~TerminalSession()
{
    if (m_pty)
        m_pty->hangup();
    ghostty_mouse_event_free(m_mouseEvent);
    ghostty_mouse_encoder_free(m_mouseEncoder);
    ghostty_key_event_free(m_keyEvent);
    ghostty_key_encoder_free(m_keyEncoder);
    ghostty_terminal_free(m_terminal);
}

bool TerminalSession::start(const QString &program, const QStringList &args, const QString &workingDirectory)
{
    if (m_pty || m_startPending)
        return false;
    m_program = program;
    m_args = args;
    m_startDir = workingDirectory.isEmpty() ? QDir::homePath() : workingDirectory;
    m_pwd = m_startDir;
    if (m_sized)
        return spawn();
    // No view yet. Do not wait forever for one, though: a hidden tab still runs.
    m_startPending = true;
    QTimer::singleShot(1000, this, [this] {
        if (m_startPending) {
            m_startPending = false;
            spawn();
        }
    });
    return true;
}

bool TerminalSession::spawn()
{
    const ShellCommand cmd = shellCommand(m_program, m_args);
    // XTGETTCAP answers come from the terminfo entry the shell was given.
    const QByteArray terminfo = cmd.env.filter(QRegularExpression(QStringLiteral("^TERM="))).value(0).mid(5).toUtf8();
    const GhosttyString terminfoName = toGhostty(terminfo);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_TERMINFO_NAME, terminfo.isEmpty() ? nullptr : &terminfoName);
    m_pty = new Pty(this);
    connect(m_pty, &Pty::dataReady, this, &TerminalSession::onData);
    connect(m_pty, &Pty::finished, this, [this](int code) {
        m_running = false;
        m_exitCode = code;
        Q_EMIT runningChanged();
        Q_EMIT finished();
    });
    if (!m_pty->start(cmd.program, cmd.args, m_startDir, cmd.env, quint16(m_cols), quint16(m_rows),
                      quint32(m_cols * m_cellW), quint32(m_rows * m_cellH))) {
        const QByteArray msg = "\r\n" TOUKO_APP_NAME ": could not start the shell\r\n";
        ghostty_terminal_vt_write(m_terminal, reinterpret_cast<const uint8_t *>(msg.constData()), size_t(msg.size()));
        afterMutation();
        return false;
    }
    m_running = true;
    Q_EMIT runningChanged();
    Q_EMIT workingDirectoryChanged();
    return true;
}

void TerminalSession::onData(const QByteArray &data)
{
    ghostty_terminal_vt_write(m_terminal, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()));
    afterMutation();
    updateBusy();
}

void TerminalSession::afterMutation()
{
    const bool alt = alternateScreen();
    const bool mouse = mouseTracking();
    if (alt != m_lastAlt || mouse != m_lastMouse) {
        m_lastAlt = alt;
        m_lastMouse = mouse;
        Q_EMIT modesChanged();
    }
    GhosttyTerminalScrollbar sb{};
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &sb) == GHOSTTY_SUCCESS
        && (sb.total != m_lastScrollTotal || sb.offset != m_lastScrollOffset)) {
        m_lastScrollTotal = sb.total;
        m_lastScrollOffset = sb.offset;
        Q_EMIT scrollChanged();
    }
    const bool sel = hasSelection();
    if (sel != m_lastSelection) {
        m_lastSelection = sel;
        Q_EMIT selectionChanged();
    }
    Q_EMIT contentChanged();
}

void TerminalSession::updateBusy()
{
    // Something other than the shell is in the foreground: closing this tab
    // asks first.
    const bool busy = m_pty && m_pty->running() && m_pty->foregroundPid() > 0
                      && m_pty->foregroundPid() != m_pty->pid();
    if (busy != m_busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
        Q_EMIT titleChanged();
    }
}

QString TerminalSession::title() const
{
    GhosttyString s{};
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_TITLE, &s) == GHOSTTY_SUCCESS && s.len)
        return fromGhostty(s);
    // No title from the shell: name the program in front of it, or the directory.
    if (m_pty && m_pty->running() && m_pty->foregroundPid() > 0 && m_pty->foregroundPid() != m_pty->pid()) {
        QFile comm(QStringLiteral("/proc/%1/comm").arg(m_pty->foregroundPid()));
        if (comm.open(QIODevice::ReadOnly)) {
            const QString name = QString::fromUtf8(comm.readAll()).trimmed();
            if (!name.isEmpty())
                return name;
        }
    }
    QString pwd = m_pwd;
    if (pwd.startsWith(QDir::homePath()))
        pwd.replace(0, QDir::homePath().size(), QStringLiteral("~"));
    return pwd.isEmpty() ? QStringLiteral(TOUKO_APP_NAME) : pwd;
}

bool TerminalSession::hasSelection() const
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    return ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SELECTION, &sel) == GHOSTTY_SUCCESS;
}

bool TerminalSession::mouseTracking() const
{
    bool v = false;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &v);
    return v;
}

bool TerminalSession::alternateScreen() const
{
    GhosttyTerminalScreen s = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &s);
    return s == GHOSTTY_TERMINAL_SCREEN_ALTERNATE;
}

bool TerminalSession::atBottom() const
{
    bool v = true;
    ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_VIEWPORT_ACTIVE, &v);
    return v;
}

qreal TerminalSession::scrollPosition() const
{
    return qreal(m_lastScrollOffset);
}

qreal TerminalSession::scrollSize() const
{
    return qreal(m_lastScrollTotal);
}

void TerminalSession::resize(int cols, int rows, int cellWidthPx, int cellHeightPx)
{
    cols = qBound(1, cols, 0xffff);
    rows = qBound(1, rows, 0xffff);
    m_sized = true;
    if (m_startPending) {
        m_cols = cols;
        m_rows = rows;
        m_cellW = cellWidthPx;
        m_cellH = cellHeightPx;
        ghostty_terminal_resize(m_terminal, quint16(cols), quint16(rows), quint32(cellWidthPx), quint32(cellHeightPx));
        m_startPending = false;
        spawn();
        Q_EMIT sizeChanged();
        afterMutation();
        return;
    }
    if (cols == m_cols && rows == m_rows && cellWidthPx == m_cellW && cellHeightPx == m_cellH)
        return;
    m_cols = cols;
    m_rows = rows;
    m_cellW = cellWidthPx;
    m_cellH = cellHeightPx;
    ghostty_terminal_resize(m_terminal, quint16(cols), quint16(rows), quint32(cellWidthPx), quint32(cellHeightPx));
    if (m_pty)
        m_pty->resize(quint16(cols), quint16(rows), quint32(cols * cellWidthPx), quint32(rows * cellHeightPx));
    Q_EMIT sizeChanged();
    afterMutation();
}

void TerminalSession::applyTheme(const Theme &theme, bool dark)
{
    m_dark = dark;
    const GhosttyColorRgb fg = rgb(theme.foreground);
    const GhosttyColorRgb bg = rgb(theme.background);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);
    if (theme.cursor) {
        const GhosttyColorRgb c = rgb(*theme.cursor);
        ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, &c);
    } else {
        ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, nullptr);
    }

    // The theme sets 0-15 (and sometimes more); the cube and the grey ramp
    // are derived from it like Ghostty's palette-generate does.
    GhosttyColorRgb base[256];
    ghostty_color_palette_default(base);
    GhosttyColorPaletteMask keep{};
    for (int i = 0; i < 256; ++i) {
        if (theme.palette[i]) {
            base[i] = rgb(*theme.palette[i]);
            if (i >= 16)
                keep.bits[i / 64] |= quint64(1) << (i % 64);
        }
    }
    GhosttyColorRgb palette[256];
    ghostty_color_palette_generate(base, &keep, &bg, &fg, false, palette);
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, palette);

    // Applications that asked to hear about scheme changes (mode 2031) do.
    if (m_pty) {
        GhosttyTerminalModeConfig mode{GHOSTTY_MODE_COLOR_SCHEME_REPORT, false};
        if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &mode) == GHOSTTY_SUCCESS && mode.value) {
            const QByteArray report = dark ? QByteArrayLiteral("\x1b[?997;1n") : QByteArrayLiteral("\x1b[?997;2n");
            m_pty->write(report.constData(), report.size());
        }
    }
    afterMutation();
}

void TerminalSession::writeRaw(const QByteArray &bytes)
{
    if (m_pty && m_pty->running())
        m_pty->write(bytes.constData(), bytes.size());
}

void TerminalSession::sendText(const QString &text)
{
    if (text.isEmpty())
        return;
    scrollToBottom();
    QByteArray bytes = text.toUtf8();
    // An input method's "\n" is the Enter key.
    bytes.replace('\n', '\r');
    writeRaw(bytes);
}

bool TerminalSession::sendKey(GhosttyKey key, GhosttyMods mods, const QString &text, uint unshiftedCodepoint,
                              GhosttyKeyAction action)
{
    ghostty_key_encoder_setopt_from_terminal(m_keyEncoder, m_terminal);
    ghostty_key_event_set_action(m_keyEvent, action);
    ghostty_key_event_set_key(m_keyEvent, key);
    ghostty_key_event_set_mods(m_keyEvent, mods);
    ghostty_key_event_set_consumed_mods(m_keyEvent, (!text.isEmpty() && (mods & GHOSTTY_MODS_SHIFT)) ? GHOSTTY_MODS_SHIFT : 0);
    ghostty_key_event_set_unshifted_codepoint(m_keyEvent, unshiftedCodepoint);
    const QByteArray utf8 = text.toUtf8();
    ghostty_key_event_set_utf8(m_keyEvent, utf8.isEmpty() ? nullptr : utf8.constData(), size_t(utf8.size()));
    ghostty_key_event_set_composing(m_keyEvent, false);

    char buf[128];
    size_t written = 0;
    GhosttyResult r = ghostty_key_encoder_encode(m_keyEncoder, m_keyEvent, buf, sizeof(buf), &written);
    QByteArray out;
    if (r == GHOSTTY_OUT_OF_SPACE) {
        out.resize(qsizetype(written));
        r = ghostty_key_encoder_encode(m_keyEncoder, m_keyEvent, out.data(), size_t(out.size()), &written);
        out.resize(qsizetype(written));
    } else {
        out = QByteArray(buf, qsizetype(written));
    }
    if (r != GHOSTTY_SUCCESS || out.isEmpty())
        return false;
    if (action != GHOSTTY_KEY_ACTION_RELEASE)
        scrollToBottom();
    writeRaw(out);
    return true;
}

bool TerminalSession::sendMouse(GhosttyMouseAction action, GhosttyMouseButton button, QPointF pos, GhosttyMods mods,
                                QSizeF screen, QSizeF cell)
{
    ghostty_mouse_encoder_setopt_from_terminal(m_mouseEncoder, m_terminal);
    GhosttyMouseEncoderSize size = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    size.screen_width = uint32_t(screen.width());
    size.screen_height = uint32_t(screen.height());
    size.cell_width = uint32_t(cell.width());
    size.cell_height = uint32_t(cell.height());
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
    const bool pressed = action == GHOSTTY_MOUSE_ACTION_PRESS
                         || (action == GHOSTTY_MOUSE_ACTION_MOTION && button != GHOSTTY_MOUSE_BUTTON_UNKNOWN);
    ghostty_mouse_encoder_setopt(m_mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &pressed);

    ghostty_mouse_event_set_action(m_mouseEvent, action);
    if (button == GHOSTTY_MOUSE_BUTTON_UNKNOWN)
        ghostty_mouse_event_clear_button(m_mouseEvent);
    else
        ghostty_mouse_event_set_button(m_mouseEvent, button);
    ghostty_mouse_event_set_mods(m_mouseEvent, mods);
    ghostty_mouse_event_set_position(m_mouseEvent, GhosttyMousePosition{float(pos.x()), float(pos.y())});
    char buf[64];
    size_t written = 0;
    if (ghostty_mouse_encoder_encode(m_mouseEncoder, m_mouseEvent, buf, sizeof(buf), &written) != GHOSTTY_SUCCESS
        || !written)
        return false;
    writeRaw(QByteArray(buf, qsizetype(written)));
    return true;
}

void TerminalSession::sendFocus(bool focused)
{
    GhosttyTerminalModeConfig mode{GHOSTTY_MODE_FOCUS_EVENT, false};
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_MODE, &mode) != GHOSTTY_SUCCESS || !mode.value)
        return;
    char buf[8];
    size_t written = 0;
    if (ghostty_focus_encode(focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, buf, sizeof(buf), &written)
        == GHOSTTY_SUCCESS)
        writeRaw(QByteArray(buf, qsizetype(written)));
}

void TerminalSession::scrollLines(int delta)
{
    if (!delta)
        return;
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    sv.value.delta = delta;
    ghostty_terminal_scroll_viewport(m_terminal, sv);
    afterMutation();
}

void TerminalSession::scrollToRow(qreal row)
{
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_ROW;
    sv.value.row = size_t(qMax<qreal>(0, row));
    ghostty_terminal_scroll_viewport(m_terminal, sv);
    afterMutation();
}

void TerminalSession::scrollToBottom()
{
    if (atBottom())
        return;
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    ghostty_terminal_scroll_viewport(m_terminal, sv);
    afterMutation();
}

void TerminalSession::scrollPrompt(int delta)
{
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA_PROMPT;
    sv.value.delta = delta;
    ghostty_terminal_scroll_viewport(m_terminal, sv);
    afterMutation();
}

GhosttyGridRef TerminalSession::refAt(int col, int row) const
{
    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    GhosttyPoint pt{};
    pt.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    pt.value.coordinate.x = quint16(qBound(0, col, m_cols - 1));
    pt.value.coordinate.y = quint32(qBound(0, row, m_rows - 1));
    ghostty_terminal_grid_ref(m_terminal, pt, &ref);
    return ref;
}

void TerminalSession::selectWordAt(int col, int row)
{
    GhosttyTerminalSelectWordOptions opts = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectWordOptions);
    opts.ref = refAt(col, row);
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_terminal_select_word(m_terminal, &opts, &sel) != GHOSTTY_SUCCESS) {
        // Blank: select the cell itself, so a drag from here still works.
        sel.start = opts.ref;
        sel.end = opts.ref;
    }
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
    afterMutation();
}

void TerminalSession::selectRange(QPoint anchor, QPoint to, bool words)
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    const GhosttyGridRef a = refAt(anchor.x(), anchor.y());
    const GhosttyGridRef b = refAt(to.x(), to.y());
    sel.start = a;
    sel.end = b;
    if (words) {
        // Grow both ends to whole words, as Ghostty's double-click drag does.
        GhosttyTerminalSelectWordBetweenOptions fwd = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectWordBetweenOptions);
        fwd.start = a;
        fwd.end = b;
        GhosttyTerminalSelectWordBetweenOptions back = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectWordBetweenOptions);
        back.start = b;
        back.end = a;
        GhosttySelection s1 = GHOSTTY_INIT_SIZED(GhosttySelection);
        GhosttySelection s2 = GHOSTTY_INIT_SIZED(GhosttySelection);
        const bool before = anchor.y() < to.y() || (anchor.y() == to.y() && anchor.x() <= to.x());
        if (ghostty_terminal_select_word_between(m_terminal, &fwd, &s1) == GHOSTTY_SUCCESS)
            sel.start = before ? s1.start : s1.end;
        if (ghostty_terminal_select_word_between(m_terminal, &back, &s2) == GHOSTTY_SUCCESS)
            sel.end = before ? s2.end : s2.start;
    }
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
    afterMutation();
}

void TerminalSession::selectAll()
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_terminal_select_all(m_terminal, &sel) == GHOSTTY_SUCCESS)
        ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
    afterMutation();
}

void TerminalSession::clearSelection()
{
    if (!hasSelection())
        return;
    ghostty_terminal_set(m_terminal, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr);
    afterMutation();
}

bool TerminalSession::selectionRange(QPoint *start, QPoint *end) const
{
    GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_terminal_get(m_terminal, GHOSTTY_TERMINAL_DATA_SELECTION, &sel) != GHOSTTY_SUCCESS)
        return false;
    GhosttySelection ordered = sel;
    ghostty_terminal_selection_ordered(m_terminal, &sel, GHOSTTY_SELECTION_ORDER_FORWARD, &ordered);
    GhosttyPointCoordinate a{}, b{};
    const bool okA = ghostty_terminal_point_from_grid_ref(m_terminal, &ordered.start, GHOSTTY_POINT_TAG_VIEWPORT, &a)
                     == GHOSTTY_SUCCESS;
    const bool okB = ghostty_terminal_point_from_grid_ref(m_terminal, &ordered.end, GHOSTTY_POINT_TAG_VIEWPORT, &b)
                     == GHOSTTY_SUCCESS;
    *start = okA ? QPoint(a.x, int(a.y)) : QPoint(0, -1);
    *end = okB ? QPoint(b.x, int(b.y)) : QPoint(m_cols - 1, m_rows);
    return okA || okB;
}

QString TerminalSession::selectedText() const
{
    GhosttyTerminalSelectionFormatOptions opts = GHOSTTY_INIT_SIZED(GhosttyTerminalSelectionFormatOptions);
    opts.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    opts.unwrap = true;
    opts.trim = true;
    uint8_t *ptr = nullptr;
    size_t len = 0;
    if (ghostty_terminal_selection_format_alloc(m_terminal, nullptr, opts, &ptr, &len) != GHOSTTY_SUCCESS)
        return {};
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(ptr), qsizetype(len));
    ghostty_free(nullptr, ptr, len);
    return text;
}

void TerminalSession::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

namespace {
struct PasteSource {
    QByteArray text;
};

bool readPaste(void *ud, GhosttyString mime, GhosttyWriter writer)
{
    auto *src = static_cast<PasteSource *>(ud);
    if (mime.len != 10 || std::memcmp(mime.ptr, "text/plain", 10) != 0)
        return false;
    return writer.write(writer.userdata, reinterpret_cast<const uint8_t *>(src->text.constData()),
                        size_t(src->text.size()));
}
} // namespace

bool TerminalSession::paste(const QString &text, bool allowUnsafe)
{
    if (text.isEmpty())
        return true;
    PasteSource src{text.toUtf8()};
    const GhosttyString mimes[] = {{reinterpret_cast<const uint8_t *>("text/plain"), 10}};
    GhosttyPaste p{};
    p.size = sizeof(p);
    p.location = GHOSTTY_CLIPBOARD_LOCATION_STANDARD;
    p.source = GHOSTTY_PASTE_SOURCE_CLIPBOARD;
    p.mimes = mimes;
    p.mimes_len = 1;
    p.reader = GhosttyMimeReader{&readPaste, &src};
    p.allow_unsafe = allowUnsafe;
    bool written = false;
    const GhosttyResult r = ghostty_terminal_paste(m_terminal, &p, &written);
    if (r == GHOSTTY_REJECTED)
        return false;
    scrollToBottom();
    return true;
}

bool TerminalSession::pasteClipboard(bool allowUnsafe)
{
    return paste(QGuiApplication::clipboard()->text(), allowUnsafe);
}

QString TerminalSession::hyperlinkAt(int col, int row) const
{
    const GhosttyGridRef ref = refAt(col, row);
    char buf[2048];
    size_t len = 0;
    GhosttyResult r = ghostty_grid_ref_hyperlink_uri(&ref, reinterpret_cast<uint8_t *>(buf), sizeof(buf), &len);
    if (r != GHOSTTY_SUCCESS || !len)
        return {};
    return QString::fromUtf8(buf, qsizetype(len));
}

void TerminalSession::hangup()
{
    if (m_pty)
        m_pty->hangup();
    if (m_running) {
        m_running = false;
        Q_EMIT runningChanged();
    }
}

// ---- libghostty-vt callbacks ------------------------------------------------

void TerminalSession::cbWritePty(GhosttyTerminal, void *ud, const uint8_t *data, size_t len)
{
    if (self(ud)->m_pty)
        self(ud)->m_pty->write(reinterpret_cast<const char *>(data), qsizetype(len));
}

void TerminalSession::cbBell(GhosttyTerminal, void *ud)
{
    Q_EMIT self(ud)->bell();
}

void TerminalSession::cbTitle(GhosttyTerminal, void *ud)
{
    Q_EMIT self(ud)->titleChanged();
}

void TerminalSession::cbPwd(GhosttyTerminal t, void *ud)
{
    GhosttyString s{};
    if (ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_PWD, &s) != GHOSTTY_SUCCESS)
        return;
    QString pwd = fromGhostty(s);
    // OSC 7 is a file:// URL with a host part.
    if (pwd.startsWith(QStringLiteral("file://"))) {
        const QUrl url(pwd);
        pwd = url.path();
    }
    if (pwd.isEmpty() || pwd == self(ud)->m_pwd)
        return;
    self(ud)->m_pwd = pwd;
    Q_EMIT self(ud)->workingDirectoryChanged();
    Q_EMIT self(ud)->titleChanged();
}

bool TerminalSession::cbSize(GhosttyTerminal, void *ud, GhosttySizeReportSize *out)
{
    out->columns = quint16(self(ud)->m_cols);
    out->rows = quint16(self(ud)->m_rows);
    out->cell_width = quint32(self(ud)->m_cellW);
    out->cell_height = quint32(self(ud)->m_cellH);
    return true;
}

bool TerminalSession::cbColorScheme(GhosttyTerminal, void *ud, GhosttyColorScheme *out)
{
    *out = self(ud)->m_dark ? GHOSTTY_COLOR_SCHEME_DARK : GHOSTTY_COLOR_SCHEME_LIGHT;
    return true;
}

bool TerminalSession::cbDeviceAttributes(GhosttyTerminal, void *, GhosttyDeviceAttributes *out)
{
    // What Ghostty answers: a VT220 with colour text and clipboard access.
    *out = {};
    out->primary.conformance_level = 62;
    out->primary.features[0] = 22;
    out->primary.features[1] = 52;
    out->primary.num_features = 2;
    out->secondary.device_type = 1;
    out->secondary.firmware_version = 10;
    out->secondary.rom_cartridge = 0;
    out->tertiary.unit_id = 0;
    return true;
}

GhosttyString TerminalSession::cbXtversion(GhosttyTerminal, void *ud)
{
    return toGhostty(self(ud)->m_xtversion);
}

void TerminalSession::cbClipboardWrite(GhosttyTerminal, void *, const GhosttyClipboardWrite *write)
{
    // OSC 52 writes go to the clipboard: that is what copying in vim or tmux
    // over ssh needs. Reads are refused below; those leak.
    GhosttyClipboardWriteReply reply{};
    reply.size = sizeof(reply);
    reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_UNSUPPORTED;
    QClipboard::Mode mode = QClipboard::Clipboard;
    if (write->location == GHOSTTY_CLIPBOARD_LOCATION_SELECTION || write->location == GHOSTTY_CLIPBOARD_LOCATION_PRIMARY)
        mode = QClipboard::Selection;
    if (write->contents_len == 0) {
        QGuiApplication::clipboard()->clear(mode);
        reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
    } else {
        auto *mime = new QMimeData;
        for (size_t i = 0; i < write->contents_len; ++i) {
            const GhosttyClipboardContent &c = write->contents[i];
            const QString type = fromGhostty(c.mime);
            const QByteArray data(reinterpret_cast<const char *>(c.data.ptr), qsizetype(c.data.len));
            if (type.isEmpty() || type == u"text/plain" || type.startsWith(u"text/plain;"))
                mime->setText(QString::fromUtf8(data));
            else
                mime->setData(type, data);
        }
        QGuiApplication::clipboard()->setMimeData(mime, mode);
        reply.result = GHOSTTY_CLIPBOARD_WRITE_RESULT_SUCCESS;
    }
    write->reply(write, &reply);
}

void TerminalSession::cbClipboardRead(GhosttyTerminal, void *, const GhosttyClipboardRead *read)
{
    GhosttyClipboardReadReply reply{};
    reply.size = sizeof(reply);
    reply.result = GHOSTTY_CLIPBOARD_READ_RESULT_DENIED;
    read->reply(read, &reply);
}

void TerminalSession::cbNotification(GhosttyTerminal, void *ud, const GhosttyTerminalDesktopNotification *n)
{
    Q_EMIT self(ud)->notify(fromGhostty(n->title), fromGhostty(n->body));
}
