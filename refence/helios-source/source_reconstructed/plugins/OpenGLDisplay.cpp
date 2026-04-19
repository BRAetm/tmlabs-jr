// OpenGLDisplay.cpp — OpenGL video display plugin

#include "OpenGLDisplay.h"
#include <QOpenGLTexture>
#include <QWidget>
#include <QVBoxLayout>

namespace Helios {

// ── GLSL shaders ─────────────────────────────────────────────────────────────

const char* HeliosGLWidget::s_vertShader = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

// BGR24 / BGRA8 — texture is uploaded as GL_RGB / GL_RGBA with swizzle
const char* HeliosGLWidget::s_fragShaderBGR = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform int uFormat; // 0=BGR24 1=BGRA8
void main() {
    vec4 c = texture(uTex, vUV);
    // OpenCV stores BGR — swap R and B
    fragColor = vec4(c.b, c.g, c.r, 1.0);
}
)";

// NV12 — Y plane + UV plane, BT.709 limited range
const char* HeliosGLWidget::s_fragShaderNV12 = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTexY;
uniform sampler2D uTexUV;
void main() {
    float y  = texture(uTexY,  vUV).r;
    vec2  uv = texture(uTexUV, vUV).rg - vec2(0.5, 0.5);
    // BT.709 YUV→RGB
    float r = y + 1.5748 * uv.y;
    float g = y - 0.1873 * uv.x - 0.4681 * uv.y;
    float b = y + 1.8556 * uv.x;
    fragColor = vec4(clamp(r,0.0,1.0), clamp(g,0.0,1.0), clamp(b,0.0,1.0), 1.0);
}
)";

// ── HeliosGLWidget ────────────────────────────────────────────────────────────

HeliosGLWidget::HeliosGLWidget(QWidget* parent) : QOpenGLWidget(parent) {}

HeliosGLWidget::~HeliosGLWidget()
{
    makeCurrent();
    if (m_texY)   glDeleteTextures(1, &m_texY);
    if (m_texUV)  glDeleteTextures(1, &m_texUV);
    if (m_texRGB) glDeleteTextures(1, &m_texRGB);
    if (m_vbo)    glDeleteBuffers(1, &m_vbo);
    if (m_vao)    glDeleteVertexArrays(1, &m_vao);
    doneCurrent();
}

void HeliosGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Fullscreen quad
    float verts[] = {
        -1.f, -1.f, 0.f, 1.f,
         1.f, -1.f, 1.f, 1.f,
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, 1.f, 0.f,
    };
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), reinterpret_cast<void*>(2*sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    // Compile BGR shader (default)
    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex,   s_vertShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, s_fragShaderBGR);
    m_program->link();

    glGenTextures(1, &m_texRGB);
    glGenTextures(1, &m_texY);
    glGenTextures(1, &m_texUV);
}

void HeliosGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void HeliosGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_dirty || m_frameW == 0) return;

    m_program->bind();
    if (m_format == 2) { // NV12
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texY);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texUV);
        m_program->setUniformValue("uTexY",  0);
        m_program->setUniformValue("uTexUV", 1);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texRGB);
        m_program->setUniformValue("uTex",    0);
        m_program->setUniformValue("uFormat", m_format);
    }

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_program->release();
    m_dirty = false;
}

void HeliosGLWidget::setFrame(int width, int height, int stride, int format, QByteArray data)
{
    m_frameW  = width;
    m_frameH  = height;
    m_format  = format;
    m_pending = data;

    makeCurrent();
    const auto* ptr = reinterpret_cast<const uint8_t*>(data.constData());

    // Recompile shader if format changed
    static int lastFmt = -1;
    if (format != lastFmt) {
        lastFmt = format;
        m_program->removeAllShaders();
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, s_vertShader);
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment,
            format == 2 ? s_fragShaderNV12 : s_fragShaderBGR);
        m_program->link();
    }

    if (format == 0) uploadBGR24(width, height, stride, ptr);
    else if (format == 1) uploadBGRA8(width, height, stride, ptr);
    else uploadNV12(width, height, ptr);

    doneCurrent();
    m_dirty = true;
    update();
}

void HeliosGLWidget::uploadBGR24(int width, int height, int stride, const uint8_t* data)
{
    glBindTexture(GL_TEXTURE_2D, m_texRGB);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void HeliosGLWidget::uploadBGRA8(int width, int height, int stride, const uint8_t* data)
{
    glBindTexture(GL_TEXTURE_2D, m_texRGB);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void HeliosGLWidget::uploadNV12(int width, int height, const uint8_t* data)
{
    // Y plane
    glBindTexture(GL_TEXTURE_2D, m_texY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // UV plane (interleaved, half resolution)
    glBindTexture(GL_TEXTURE_2D, m_texUV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, width/2, height/2, 0, GL_RG, GL_UNSIGNED_BYTE,
                 data + width * height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// ── OpenGLDisplayPlugin ───────────────────────────────────────────────────────

OpenGLDisplayPlugin::OpenGLDisplayPlugin(QObject* parent) : QObject(parent) {}
OpenGLDisplayPlugin::~OpenGLDisplayPlugin() { shutdown(); }

void OpenGLDisplayPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
    m_readTimer = new QTimer(this);
    connect(m_readTimer, &QTimer::timeout, this, &OpenGLDisplayPlugin::readFromSharedMemory);

    // Open ring buffer reader; start polling once SHM is available
    if (m_ringReader.open(SharedMemoryManager::blockName("VIDEO")))
        m_readTimer->start(16); // ~60 fps polling
}

void OpenGLDisplayPlugin::shutdown()
{
    if (m_readTimer) m_readTimer->stop();
    m_ringReader.close();
}

QWidget* OpenGLDisplayPlugin::createWidget(QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* layout    = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    m_glWidget = new HeliosGLWidget(container);
    m_glWidget->setMinimumSize(640, 360);
    layout->addWidget(m_glWidget);

    return container;
}

void OpenGLDisplayPlugin::onFrame(int width, int height, int stride, int format, QByteArray data)
{
    if (m_glWidget)
        m_glWidget->setFrame(width, height, stride, format, data);
}

void OpenGLDisplayPlugin::readFromSharedMemory()
{
    VideoFrameData frame = {};
    if (!m_ringReader.read(frame)) return;

    QByteArray data(reinterpret_cast<const char*>(frame.data), static_cast<int>(frame.size));
    if (m_glWidget)
        m_glWidget->setFrame(frame.width, frame.height, frame.stride, frame.format, data);
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::OpenGLDisplayPlugin();
}
