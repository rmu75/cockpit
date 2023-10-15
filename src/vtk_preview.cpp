/*
 * vtk_preview.cpp
 *
 * path preview
 * (c) 2023 Robert Schöftner <rs@unfoo.net>
 */

#include "vtk_preview.hpp"

#include "imgui.h"
#include "shcom.hh"
#include "vtkActor.h"
#include "vtkAxesActor.h"
#include "vtkCamera.h"
#include "vtkConeSource.h"
#include "vtkCubeAxesActor.h"
#include "vtkCylinderSource.h"
#include "vtkPolyDataMapper.h"
#include "vtkSmartPointer.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"

namespace ImCNC {

extern ShCom emc;

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
    const auto& axis = emc.status().motion.axis;

    SetBounds(axis[0].minPositionLimit, axis[0].maxPositionLimit,
              axis[1].minPositionLimit, axis[1].maxPositionLimit,
              axis[2].minPositionLimit, axis[2].maxPositionLimit);
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

class ToolActor : public vtkActor
{
public:
  static ToolActor* New() { return new ToolActor(); }

  ToolActor()
  {
    auto transform = vtkSmartPointer<vtkTransform>::New();
    m_tool = vtkSmartPointer<vtkCylinderSource>::New();
    // m_tool->SetHeight(height / 2.0);
    // m_tool->SetCenter(height / 4.0, 0, 0);
    // m_tool->SetRadius(height / 4.0);
    // m_tool->SetResolution(64);
    // transform->RotateWXYZ(90, 0, 1, 0);
    m_tool->SetHeight(height / 2.0);
    m_tool->SetCenter(0, height / 4.0, 0);
    m_tool->SetRadius(height / 4.0);
    m_tool->SetResolution(64);
    transform->RotateWXYZ(180, 1, 0, 0);
    auto transform_filter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    transform_filter->SetTransform(transform);
    transform_filter->SetInputConnection(m_tool->GetOutputPort());
    transform_filter->Update();

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(transform_filter->GetOutputPort());

    SetMapper(mapper);
  }

  void set_position(EmcPose position)
  {
    // printf("set position %f %f %f\n", position.tran.x, position.tran.y,
    //        position.tran.z);
    auto transform = vtkSmartPointer<vtkTransform>::New();
    transform->Translate(position.tran.x, position.tran.y, position.tran.z);
    SetUserTransform(transform);
    // SetPosition(position.tran.x, position.tran.y, position.tran.z);
  }

private:
  vtkSmartPointer<vtkCylinderSource> m_tool;
  double height = 50.0;
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
  m_tool = vtkSmartPointer<ToolActor>::New();
  machine->SetCamera(camera);
  m_viewer.addActor(axes);
  m_viewer.addActor(machine);
  m_viewer.addActor(m_tool);
}

void VtkPreview::open_file(std::string path) {}

void VtkPreview::_update_camera(vtkCamera& camera, double x, double y, double z,
                                double vx, double vy, double vz)
{
  camera.SetPosition(x, y, z);
  camera.SetFocalPoint(0, 0, 0);
  camera.SetViewUp(vx, vy, vz);
  camera.SetClippingRange(0.01, 10000);
  m_viewer.getInteractor()->ReInitialize();
}
void VtkPreview::show()
{
  ImGui::SetNextWindowSize(ImVec2(360, 240), ImGuiCond_FirstUseEver);

  ImGui::Begin("preview");
  auto renderer = m_viewer.getRenderer();
  auto camera = renderer->GetActiveCamera();

  if (ImGui::Button("ORTHO")) {
    camera->ParallelProjectionOn();
    m_viewer.getInteractor()->ReInitialize();
  }
  ImGui::SameLine();
  if (ImGui::Button("PERSP")) {
    camera->ParallelProjectionOff();
    m_viewer.getInteractor()->ReInitialize();
  }
  ImGui::SameLine();
  if (ImGui::Button("P")) {
    _update_camera(*camera, 1000, -1000, 1000, 0, 0, 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("X")) {
    // camera distance should probably be configurable somewhere
    _update_camera(*camera, 0, -1000, 0, 0, 0, 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Y")) {
    _update_camera(*camera, 1000, 0, 0, 0, 0, 1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Z")) {
    _update_camera(*camera, 0, 0, 1000, 0, 1, 0);
  }
  // for lathe
  // ImGui::SameLine();
  // if (ImGui::Button("XZ")) {
  //   _update_camera(*camera, 0, 1000, 0, 1, 0, 0);
  // }
  // ImGui::SameLine();
  // if (ImGui::Button("X2")) {
  //   _update_camera(*camera, 0, -1000, 0, -1, 0, 0);
  // }
  ImGui::SameLine();
  if (ImGui::Button("+")) {
    if (camera->GetParallelProjection()) {
      camera->SetParallelScale(camera->GetParallelScale() * (1.0 / 1.1));
    }
    else {
      camera->Zoom(1.1);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("-")) {
    if (camera->GetParallelProjection()) {
      camera->SetParallelScale(camera->GetParallelScale() * 1.1);
    }
    else {
      camera->Zoom(1.0 / 1.1);
    }
  }

  m_tool->set_position(emc.status().motion.traj.actualPosition);
  m_viewer.render();
  ImGui::End();
}

} // namespace ImCNC