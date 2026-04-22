#include "DisplayPlugin.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QPainter>
#include <QPalette>

namespace Labs {

// ── DisplaySurface ──────────────────────────────────────────────────────────

DisplaySurface::DisplaySurface(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(20, 20, 28));
    setPalette(pal);
    setMinimumSize(640, 360);
}

void DisplaySurface::setImage(const QImage& img)
{
    {
        QMutexLocker lock(&m_mx);
        m_image = img;
    }
    update();
}

void DisplaySurface::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QImage img;
    {
        QMutexLocker lock(&m_mx);
        img = m_image;
    }
    if (img.isNull()) {
        p.setPen(QColor(220, 220, 220));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Display — no source"));
        return;
    }
    const QSize target = size();
    const QImage scaled = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = (target.width()  - scaled.width())  / 2;
    const int y = (target.height() - scaled.height()) / 2;
    p.drawImage(x, y, scaled);
}

// ── DisplayPlugin ───────────────────────────────────────────────────────────

DisplayPlugin::DisplayPlugin()  = default;
DisplayPlugin::~DisplayPlugin() = default;

void DisplayPlugin::initialize(const PluginContext&) {}
void DisplayPlugin::shutdown() {}

QWidget* DisplayPlugin::createWidget(QWidget* parent)
{
    m_surface = new DisplaySurface(parent);
    return m_surface;
}

void DisplayPlugin::pushFrame(const Frame& frame)
{
    if (!frame.isValid() || !m_surface) return;

    // BGRA8 bytes map directly onto QImage::Format_ARGB32 on little-endian.
    QImage::Format qfmt = QImage::Format_Invalid;
    switch (frame.format) {
        case PixelFormat::BGRA8: qfmt = QImage::Format_ARGB32; break;
        case PixelFormat::RGBA8: qfmt = QImage::Format_RGBA8888; break;
        default: return;
    }
    // Copy so the QImage owns its memory even after the Frame goes out of scope.
    QImage img(reinterpret_cast<const uchar*>(frame.data.constData()),
               frame.width, frame.height, frame.stride, qfmt);
    img = img.copy();

    QPointer<DisplaySurface> surface = m_surface;
    QMetaObject::invokeMethod(surface, [surface, img]() {
        if (surface) surface->setImage(img);
    }, Qt::QueuedConnection);
}

} // namespace Labs

extern "C" __declspec(dllexport) Labs::IPlugin* createPlugin()
{
    return new Labs::DisplayPlugin();
}
