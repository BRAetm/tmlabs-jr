#pragma once

#include <QApplication>
#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

namespace Labs {

// Mirrors the logo Canvas from MainWindow.xaml. Accent + highlight tints come
// from the application palette so the logo retints with the theme.
// LabsTheme sets QApplication's palette Highlight/Accent colors whenever the
// theme is applied.
class LabsLogoWidget : public QWidget {
    Q_OBJECT
public:
    explicit LabsLogoWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(30, 30);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QColor accent = qApp->palette().color(QPalette::Highlight);
        const QColor hi = accent.lighter(130);
        QColor soft = accent.lighter(150);
        soft.setAlphaF(0.6);

        // "L" glyph
        QPainterPath path;
        path.moveTo( 4,  4); path.lineTo(13,  4);
        path.lineTo(13, 19); path.lineTo(26, 19);
        path.lineTo(26, 26); path.lineTo( 4, 26);
        path.closeSubpath();
        p.fillPath(path, accent);

        // Signal pulse
        p.setBrush(hi);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRect(18, 6, 5, 5));

        // Accent bar
        p.fillRect(24, 22, 4, 2, soft);
    }
};

} // namespace Labs
