#pragma once

#include "InputTypes.h"

#include <QWidget>
#include <deque>

namespace Labs {

class ControllerMonitorWidget : public QWidget, public IControllerSink {
    Q_OBJECT
public:
    explicit ControllerMonitorWidget(QWidget* parent = nullptr);

    void pushState(const ControllerState& state) override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    static constexpr int kHistLen = 500;

    struct Sample { float lx, ly, rx, ry, lt, rt; };

    ControllerState    m_state;
    bool               m_hasState = false;
    std::deque<Sample> m_history;
};

} // namespace Labs
