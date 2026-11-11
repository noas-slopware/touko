// SPDX-License-Identifier: GPL-2.0-or-later
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QVariantMap>

namespace {

QStringList themeDirs()
{
    QStringList dirs;
    // User themes first, so a copy in ~/.config wins over the packaged one.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    dirs << config + QStringLiteral("/ghostty/themes");
    for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        dirs << d + QStringLiteral("/ghostty/themes");
    return dirs;
}

QColor parseColor(QString v)
{
    v = v.trimmed();
    if (v.size() >= 2 && v.startsWith(u'"') && v.endsWith(u'"'))
        v = v.mid(1, v.size() - 2);
    if (!v.startsWith(u'#') && v.size() == 6)
        v.prepend(u'#');
    return QColor::fromString(v);
}

std::optional<Theme> parse(const QString &name, const QByteArray &text)
{
    Theme t;
    t.name = name;
    bool any = false;
    for (const QByteArray &rawLine : text.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const qsizetype eq = line.indexOf(u'=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == u"palette") {
            const qsizetype eq2 = value.indexOf(u'=');
            bool ok = false;
            const int idx = value.left(eq2).trimmed().toInt(&ok);
            const QColor c = parseColor(value.mid(eq2 + 1));
            if (ok && idx >= 0 && idx < 256 && c.isValid())
                t.palette[idx] = c;
            continue;
        }
        const QColor c = parseColor(value);
        if (!c.isValid())
            continue;
        any = true;
        if (key == u"background")
            t.background = c;
        else if (key == u"foreground")
            t.foreground = c;
        else if (key == u"cursor-color")
            t.cursor = c;
        else if (key == u"cursor-text")
            t.cursorText = c;
        else if (key == u"selection-background")
            t.selectionBackground = c;
        else if (key == u"selection-foreground")
            t.selectionForeground = c;
    }
    if (!any || !t.background.isValid() || !t.foreground.isValid())
        return std::nullopt;
    return t;
}

} // namespace

bool Theme::isDark() const
{
    return background.lightnessF() < 0.5;
}

// Copies of Ghostty's "GitHub Light Default" and "Breeze" themes, for a
// system without Ghostty's theme files.
Theme Theme::builtinLight()
{
    static const QByteArray text = R"(palette = 0=#24292f
palette = 1=#cf222e
palette = 2=#116329
palette = 3=#4d2d00
palette = 4=#0969da
palette = 5=#8250df
palette = 6=#1b7c83
palette = 7=#6e7781
palette = 8=#57606a
palette = 9=#a40e26
palette = 10=#1a7f37
palette = 11=#633c01
palette = 12=#218bff
palette = 13=#a475f9
palette = 14=#3192aa
palette = 15=#8c959f
background = #ffffff
foreground = #1f2328
cursor-color = #0969da
cursor-text = #3c9cff
selection-background = #1f2328
selection-foreground = #ffffff
)";
    return *parse(QStringLiteral("GitHub Light Default"), text);
}

Theme Theme::builtinDark()
{
    static const QByteArray text = R"(palette = 0=#31363b
palette = 1=#ed1515
palette = 2=#11d116
palette = 3=#f67400
palette = 4=#1d99f3
palette = 5=#9b59b6
palette = 6=#1abc9c
palette = 7=#eff0f1
palette = 8=#7f8c8d
palette = 9=#c0392b
palette = 10=#1cdc9a
palette = 11=#fdbc4b
palette = 12=#3daee9
palette = 13=#8e44ad
palette = 14=#16a085
palette = 15=#fcfcfc
background = #31363b
foreground = #eff0f1
cursor-color = #eff0f1
cursor-text = #31363b
selection-background = #eff0f1
selection-foreground = #31363b
)";
    return *parse(QStringLiteral("Breeze"), text);
}

std::optional<Theme> Theme::load(const QString &name)
{
    if (name.isEmpty() || name.contains(u'/'))
        return std::nullopt;
    for (const QString &dir : themeDirs()) {
        QFile f(dir + u'/' + name);
        if (f.open(QIODevice::ReadOnly))
            return parse(name, f.readAll());
    }
    if (name == builtinLight().name)
        return builtinLight();
    if (name == builtinDark().name)
        return builtinDark();
    return std::nullopt;
}

void ThemeList::scan() const
{
    if (m_scanned)
        return;
    m_scanned = true;
    QStringList names;
    for (const QString &dir : themeDirs()) {
        for (const QString &n : QDir(dir).entryList(QDir::Files | QDir::Readable)) {
            if (!names.contains(n))
                names << n;
        }
    }
    for (const QString &builtin : {Theme::builtinLight().name, Theme::builtinDark().name}) {
        if (!names.contains(builtin))
            names << builtin;
    }
    names.sort(Qt::CaseInsensitive);
    for (const QString &n : names) {
        const auto t = Theme::load(n);
        if (!t)
            continue;
        (t->isDark() ? m_dark : m_light) << n;
    }
}

QStringList ThemeList::lightThemes() const
{
    scan();
    return m_light;
}

QStringList ThemeList::darkThemes() const
{
    scan();
    return m_dark;
}

QVariantMap ThemeList::preview(const QString &name) const
{
    QVariantMap m;
    const auto t = Theme::load(name);
    if (!t)
        return m;
    m[QStringLiteral("background")] = t->background;
    m[QStringLiteral("foreground")] = t->foreground;
    QVariantList colors;
    for (int i = 0; i < 16; ++i)
        colors << (t->palette[i] ? QVariant(*t->palette[i]) : QVariant(t->foreground));
    m[QStringLiteral("palette")] = colors;
    return m;
}
