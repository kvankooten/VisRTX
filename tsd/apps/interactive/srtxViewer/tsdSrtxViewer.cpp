// Copyright 2026 NVIDIA Corporation
// SPDX-License-Identifier: Apache-2.0

// tsd_io
#include <tsd/io/procedural.hpp>
// tsd_ui_imgui
#include <tsd/ui/imgui/Application.h>
#include <tsd/ui/imgui/windows/Animations.h>
#include <tsd/ui/imgui/windows/DatabaseEditor.h>
#include <tsd/ui/imgui/windows/LayerTree.h>
#include <tsd/ui/imgui/windows/Log.h>
#include <tsd/ui/imgui/windows/ObjectEditor.h>
#include <tsd/ui/imgui/windows/Timeline.h>
#include <tsd/ui/imgui/windows/Viewport.h>
// local
#include "SrtxControlPanel.h"
#include "SrtxViewport.h"
// std
#include <chrono>

namespace tsd_srtx_viewer {

using TSDApplication = tsd::ui::imgui::Application;
namespace tsd_ui = tsd::ui::imgui;

class Application : public TSDApplication
{
 public:
  Application(int argc, const char *argv[]) : TSDApplication(argc, argv) {}
  ~Application() override = default;

  anari_viewer::WindowArray setupWindows() override
  {
    auto windows = TSDApplication::setupWindows();

    auto *core = appCore();

    // Standard viewport for local rendering
    auto *viewport =
        new tsd_ui::Viewport(this, &core->view.manipulator, "Viewport");
    // SRTX remote viewport
    auto *srtxViewport =
        new tsd_srtx::SrtxViewport(this, &core->view.manipulator);
    // Dockable control panel that drives the SRTX viewport's device + settings
    auto *srtxControl = new tsd_srtx::SrtxControlPanel(this, srtxViewport);

    auto *animations = new tsd_ui::Animations(this);
    auto *timeline = new tsd_ui::Timeline(this);
    auto *log = new tsd_ui::Log(this);
    auto *dbeditor = new tsd_ui::DatabaseEditor(this);
    auto *oeditor = new tsd_ui::ObjectEditor(this);
    auto *otree = new tsd_ui::LayerTree(this);

    windows.emplace_back(viewport);
    windows.emplace_back(srtxViewport);
    windows.emplace_back(srtxControl);
    windows.emplace_back(animations);
    windows.emplace_back(timeline);
    windows.emplace_back(dbeditor);
    windows.emplace_back(oeditor);
    windows.emplace_back(otree);
    windows.emplace_back(log);

    setWindowArray(windows);

    // Populate scene
    auto populateScene = [this, vp = viewport, core = core]() {
      auto loadStart = std::chrono::steady_clock::now();
      core->setupSceneFromCommandLine();
      auto loadEnd = std::chrono::steady_clock::now();
      auto loadSeconds =
          std::chrono::duration<float>(loadEnd - loadStart).count();

      auto &scene = core->tsd.scene;

      if (scene.numberOfObjects(ANARI_LIGHT) == 0)
      {
        tsd::core::logStatus("...setting up default light");
        tsd::io::generate_default_lights(scene);
      }

      tsd::core::logStatus("...scene load complete! (%.3fs)", loadSeconds);
      tsd::core::logStatus(
          "%s", tsd::core::objectDBInfo(scene.objectDB()).c_str());
      core->tsd.sceneLoadComplete = true;

      if (commandLineOptions()->useDefaultRenderer)
        vp->setLibraryToDefault();
    };

    showTaskModal(populateScene, "Please Wait: Loading Scene...");

    return windows;
  }

  const char *getDefaultLayout() const override
  {
    return R"layout(
[Window][MainDockSpace]
Pos=0,26
Size=1920,1054
Collapsed=0

[Window][Viewport]
Pos=549,26
Size=685,797
Collapsed=0
DockId=0x00000006,0

[Window][SRTX Viewport]
Pos=1237,26
Size=683,797
Collapsed=0
DockId=0x00000007,0

[Window][Layers]
Pos=0,26
Size=547,548
Collapsed=0
DockId=0x00000008,0

[Window][Object Editor]
Pos=0,576
Size=547,504
Collapsed=0
DockId=0x00000009,0

[Window][Log]
Pos=549,825
Size=1371,255
Collapsed=0
DockId=0x00000005,0

[Window][Database Editor]
Pos=0,576
Size=547,504
Collapsed=0
DockId=0x00000009,1

[Window][SRTX Control]
Pos=0,576
Size=547,504
Collapsed=0
DockId=0x00000009,2

[Window][Animations]
Pos=0,26
Size=547,548
Collapsed=0
DockId=0x00000008,1

[Window][Timeline]
Pos=549,825
Size=1371,255
Collapsed=0
DockId=0x00000005,1

[Docking][Data]
DockSpace         ID=0x80F5B4C5 Window=0x079D3A04 Pos=0,26 Size=1920,1054 Split=X
  DockNode        ID=0x00000001 Parent=0x80F5B4C5 SizeRef=547,1105 Split=Y Selected=0xCD8384B1
    DockNode      ID=0x00000008 Parent=0x00000001 SizeRef=547,575 Selected=0xCD8384B1
    DockNode      ID=0x00000009 Parent=0x00000001 SizeRef=547,528 Selected=0x82B4C496
  DockNode        ID=0x00000002 Parent=0x80F5B4C5 SizeRef=1371,1105 Split=Y
    DockNode      ID=0x00000004 Parent=0x00000002 SizeRef=1370,797 Split=X Selected=0xC450F867
      DockNode    ID=0x00000006 Parent=0x00000004 SizeRef=685,848 CentralNode=1 Selected=0xC450F867
      DockNode    ID=0x00000007 Parent=0x00000004 SizeRef=683,848 Selected=0xA3219422
    DockNode      ID=0x00000005 Parent=0x00000002 SizeRef=1370,255 Selected=0x139FDA3F
)layout";
  }
};

} // namespace tsd_srtx_viewer

///////////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[])
{
  {
    tsd_srtx_viewer::Application app(argc, argv);
    app.run(1920, 1080, "TSD SRTX Viewer");
  }

  return 0;
}
