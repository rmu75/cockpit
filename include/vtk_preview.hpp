/*
 * vtk_preview.cpp
 *
 * path preview
 * (c) 2023 Robert Schöftner <rs@unfoo.net>
 */

#pragma once

#include "VtkViewer.h"

#include <memory>

class vtkCamera;
class vtkRenderer;

namespace ImCNC {

class ToolActor;

class VtkPreview
{
public:
  VtkPreview();
  ~VtkPreview();
  void open_file(std::string path);
  void show();

private:
  void _update_camera(vtkCamera& camera, double x, double y, double z,
                      double vx, double vy, double vz);
  VtkViewer m_viewer;
  std::unique_ptr<ToolActor> m_tool_actor;
};

} // namespace ImCNC