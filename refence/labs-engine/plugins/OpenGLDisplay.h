#pragma once
// OpenGLDisplay.h — OpenGL video display plugin with GLSL shaders
// Renders BGR24, BGRA8, NV12 frames using shader-based colorspace conversion

#include "../helios_core/IPlugin.h"
#include "../helios_core/SharedMemory.h"
#include <QObject>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QTimer>

namespace Helios {

class HeliosGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit HeliosGLWidget(QWidget* parent = nullptr);
    ~HeliosGLWidget() override;

    void setFrame(int width, int height, int stride, int format, QByteArray data);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void uploadBGR24(int width, int height, int stride, const uint8_t* data);
    void uploadBGRA8(int width, int height, int stride, const uint8_t* data);
    void uploadNV12(int width, int height, const uint8_t* data);

    QOpenGLShaderProgram* m_program   = nullptr;
    unsigned int          m_vao       = 0;
    unsigned int          m_vbo       = 0;
    unsigned int          m_texY      = 0; // for NV12 luma
    unsigned int          m_texUV     = 0; // for NV12 chroma
    unsigned int          m_texRGB    = 0; // for BGR24/BGRA8
    int                   m_frameW    = 0;
    int                   m_frameH    = 0;
    int                   m_format    = 0; // 0=BGR24 1=BGRA8 2=NV12
    QByteArray            m_pending;
    bool                  m_dirty     = false;

    // GLSL shaders:
    // BGR24/BGRA8: simple sampler2D, swizzle RGB→BGR
    // NV12: dual sampler (Y plane + UV plane), BT.709 YUV→RGB matrix
    static const char* s_vertShader;
    static const char* s_fragShaderBGR;
    static const char* s_fragShaderNV12;
};

class OpenGLDisplayPlugin : public QObject, public IUIPlugin {
    Q_OBJECT
public:
    explicit OpenGLDisplayPlugin(QObject* parent = nullptr);
    ~OpenGLDisplayPlugin() override;

    QString  name()        const override { return "OpenGLDisplay"; }
    QString  author()      const override { return "Helios"; }
    QString  description() const override { return "OpenGL video display (BGR24/BGRA8/NV12)"; }
    QString  version()     const override { return "1.0.0"; }

    void     initialize(const PluginContext& ctx) override;
    void     shutdown() override;
    QObject* qobject() override { return this; }

    QWidget* createWidget(QWidget* parent) override;

public slots:
    // Connect to DSCapture/MFCapture/OpencvCapture frameReceived signal
    void onFrame(int width, int height, int stride, int format, QByteArray data);
    // Reads from video ring buffer shared memory
    void readFromSharedMemory();

private:
    HeliosGLWidget*       m_glWidget  = nullptr;
    VideoRingBufferReader m_ringReader;
    QTimer*               m_readTimer = nullptr;
    PluginContext          m_ctx;
};

} // namespace Helios
