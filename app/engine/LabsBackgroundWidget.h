#pragma once

#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QString>
#include <QWidget>

namespace Labs {

enum class ImageStretch { UniformToFill, Uniform, Fill };

// Root widget for the main window. Paints an optional background image
// behind all child widgets; rails + panels render on top with alpha from
// the QSS so the image bleeds through. Matches the "image" preset from
// LabsSharp's ThemeService.
class LabsBackgroundWidget : public QWidget {
    Q_OBJECT
public:
    explicit LabsBackgroundWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setAutoFillBackground(true);
    }

    void setImage(const QString& path) {
        if (path.isEmpty()) { m_image = QImage(); update(); return; }
        QImage img(path);
        m_image = img;
        update();
    }

    void setStretch(ImageStretch s) { m_stretch = s; update(); }
    void setOpacity(qreal v) { m_opacity = qBound(0.0, v, 1.0); update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), palette().window());

        if (m_image.isNull()) return;

        p.setOpacity(m_opacity);

        const QSize target = size();
        const QSize src    = m_image.size();

        QRect dst;
        switch (m_stretch) {
            case ImageStretch::Fill:
                dst = rect();
                break;
            case ImageStretch::Uniform: {
                QSize fitted = src.scaled(target, Qt::KeepAspectRatio);
                dst = QRect(QPoint((target.width()-fitted.width())/2,
                                   (target.height()-fitted.height())/2), fitted);
                break;
            }
            case ImageStretch::UniformToFill:
            default: {
                QSize fitted = src.scaled(target, Qt::KeepAspectRatioByExpanding);
                dst = QRect(QPoint((target.width()-fitted.width())/2,
                                   (target.height()-fitted.height())/2), fitted);
                break;
            }
        }
        p.drawImage(dst, m_image);
    }

private:
    QImage       m_image;
    ImageStretch m_stretch = ImageStretch::UniformToFill;
    qreal        m_opacity = 1.0;
};

} // namespace Labs
