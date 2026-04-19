// FileExplorer.cpp — File browser with GCVWorker script templates

#include "FileExplorer.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QFileSystemModel>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QInputDialog>
#include <QFile>
#include <QDir>

namespace Helios {

// Built-in GCVWorker Python script templates
static const struct { const char* name; const char* content; } kTemplates[] = {
    { "Shot Meter (Arrow2)",
      "# GCVWorker — Arrow2 shot meter template\n"
      "from shot import Arrow2, load_meter\n"
      "from nba2k_helper import Helper\n\n"
      "helper = Helper()\n"
      "meter = Arrow2()\n"
      "helper.meter_run(meter)\n" },
    { "Skeleton Shooter",
      "# GCVWorker — Skeleton shooter template\n"
      "from skele import Shooter, Parser\n"
      "from nba2k_helper import Helper\n\n"
      "helper = Helper()\n"
      "helper.skele_run()\n" },
    { "Aim Engine",
      "# GCVWorker — Aim engine template\n"
      "import cv2\n"
      "import numpy as np\n"
      "# Connect to shared memory ring buffer\n"
      "# TODO: implement aim logic\n" },
};

FileExplorerPlugin::FileExplorerPlugin(QObject* parent) : QObject(parent) {}

void FileExplorerPlugin::initialize(const PluginContext& ctx)
{
    m_ctx = ctx;
    // Root: %APPDATA%\HeliosProject\Helios\scripts
    m_rootPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scripts";
    QDir().mkpath(m_rootPath);
}

QWidget* FileExplorerPlugin::createWidget(QWidget* parent)
{
    auto* w   = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);

    auto* bar         = new QHBoxLayout;
    auto* templateCombo = new QComboBox(w);
    auto* btnNew      = new QPushButton("New from Template", w);
    auto* btnReveal   = new QPushButton("Open Folder", w);

    templateCombo->addItem("-- Select Template --");
    for (const auto& t : kTemplates)
        templateCombo->addItem(t.name);

    bar->addWidget(templateCombo, 1);
    bar->addWidget(btnNew);
    bar->addWidget(btnReveal);
    lay->addLayout(bar);

    m_model = new QFileSystemModel(w);
    m_model->setRootPath(m_rootPath);
    m_model->setNameFilters({"*.py", "*.gpc", "*.gpc3", "*.json", "*.txt"});
    m_model->setNameFilterDisables(false);

    m_tree = new QTreeView(w);
    m_tree->setModel(m_model);
    m_tree->setRootIndex(m_model->index(m_rootPath));
    m_tree->hideColumn(1); // size
    m_tree->hideColumn(2); // type
    m_tree->header()->setStretchLastSection(true);
    lay->addWidget(m_tree);

    connect(m_tree, &QTreeView::doubleClicked, this, &FileExplorerPlugin::onItemDoubleClicked);
    connect(btnNew, &QPushButton::clicked, [=]() {
        int idx = templateCombo->currentIndex() - 1;
        if (idx < 0) return;
        const auto& tmpl = kTemplates[idx];

        bool ok = false;
        QString fname = QInputDialog::getText(w, "New Script", "File name:", QLineEdit::Normal,
                                              QString(tmpl.name).replace(' ', '_').toLower() + ".py", &ok);
        if (!ok || fname.isEmpty()) return;
        if (!fname.endsWith(".py")) fname += ".py";

        QString path = m_rootPath + "/" + fname;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(tmpl.content);

        emit fileSelected(path);
    });

    connect(btnReveal, &QPushButton::clicked, [=]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_rootPath));
    });

    // Right-click context menu
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, [=](const QPoint& pos) {
        QModelIndex idx = m_tree->indexAt(pos);
        if (!idx.isValid()) return;
        QString path = m_model->filePath(idx);

        QMenu menu;
        menu.addAction("Open in Editor", [=]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        menu.addAction("Load Script", [=]() { emit fileSelected(path); });
        menu.addSeparator();
        menu.addAction("Delete", [=]() { QFile::remove(path); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });

    return w;
}

void FileExplorerPlugin::onItemDoubleClicked(const QModelIndex& index)
{
    QString path = m_model->filePath(index);
    if (!m_model->isDir(index))
        emit fileSelected(path);
}

void FileExplorerPlugin::onNewFromTemplate()
{
    emit templateRequested("default");
}

} // namespace Helios

extern "C" __declspec(dllexport) Helios::IPlugin* createPlugin() {
    return new Helios::FileExplorerPlugin();
}
