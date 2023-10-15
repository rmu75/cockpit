/*
 * vtk_preview.cpp
 *
 * path preview
 * (c) 2023 Robert Schöftner <rs@unfoo.net>
 */

#pragma once

#include "VtkViewer.h"

class vtkCamera;
class vtkRenderer;

namespace ImCNC {

class ToolActor;

class VtkPreview
{
public:
  VtkPreview();

  void open_file(std::string path);
  void show();

private:
  void _update_camera(vtkCamera& camera, double x, double y, double z,
                      double vx, double vy, double vz);
  VtkViewer m_viewer;
  vtkSmartPointer<ToolActor> m_tool;
};

} // namespace ImCNC