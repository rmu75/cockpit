/*
 * vtk_preview.cpp
 *
 * path preview
 * (c) 2023 Robert Schöftner <rs@unfoo.net>
 */

#include "vtk_preview.hpp"

#include "imgui.h"
#include "vtkActor.h"
#include "vtkAxesActor.h"
#include "vtkCamera.h"
#include "vtkCubeAxesActor.h"
#include "vtkSmartPointer.h"
#include "vtkTransform.h"

namespace ImCNC {

class AxesActor : public vtkAxesActor
{
public:
  static AxesActor* New() { return new AxesActor(); };

  AxesActor()
  {
    double length = 20.0;

    AxisLabelsOff();
    SetShaftTypeToLine();
    SetTipTypeToCone();
    SetTotalLength(length, length, length);
  }
};

class MachineActor : public vtkCubeAxesActor
{
public:
  static MachineActor* New() { return new MachineActor(); }

  MachineActor()
  {
    SetBounds(-10, 3200, -10, 1300, -120, 20);
    SetXLabelFormat("%6.3f");
    SetYLabelFormat("%6.3f");
    SetZLabelFormat("%6.3f");

    SetFlyModeToStaticEdges();

    SetXUnits("mm");
    SetYUnits("mm");
    SetZUnits("mm");

    DrawXGridlinesOn();
    DrawYGridlinesOn();
    DrawZGridlinesOn();

    SetGridLineLocation(VTK_GRID_LINES_FURTHEST);
  }
};

VtkPreview::VtkPreview()
{
  auto camera = vtkSmartPointer<vtkCamera>::New();
  camera->ParallelProjectionOn();
  camera->SetClippingRange(0.01, 10000);
  auto renderer = m_viewer.getRenderer();
  renderer->SetActiveCamera(camera);
  auto axes = vtkSmartPointer<AxesActor>::New();
  auto machine = vtkSmartPointer<MachineActor>::New();
  machine->SetCamera(camera);
  m_viewer.addActor(axes);
  m_viewer.addActor(machine);
}

void VtkPreview::open_file(std::string path) {}

void VtkPreview::show()
{
  ImGui::SetNextWindowSize(ImVec2(360, 240), ImGuiCond_FirstUseEver);

  ImGui::Begin("preview");
  m_viewer.render();
  ImGui::End();
}

} // namespace ImCNC