// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <optional>

// Terminal colours, read from Ghostty theme files
// (/usr/share/ghostty/themes and ~/.config/ghostty/themes).
struct Theme {
    QString name;
    QColor foreground;
    QColor background;
    std::optional<QColor> cursor;
    std::optional<QColor> cursorText;
    std::optional<QColor> selectionBackground;
    std::optional<QColor> selectionForeground;
    // Entries the theme sets; the rest stay libghostty's defaults.
    std::array<std::optional<QColor>, 256> palette;

    bool isDark() const;
    static Theme builtinLight();
    static Theme builtinDark();
    static std::optional<Theme> load(const QString &name);
};

class ThemeList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList lightThemes READ lightThemes CONSTANT)
    Q_PROPERTY(QStringList darkThemes READ darkThemes CONSTANT)
public:
    using QObject::QObject;
    QStringList lightThemes() const;
    QStringList darkThemes() const;
    Q_INVOKABLE QVariantMap preview(const QString &name) const;

private:
    void scan() const;
    mutable bool m_scanned = false;
    mutable QStringList m_light;
    mutable QStringList m_dark;
};
