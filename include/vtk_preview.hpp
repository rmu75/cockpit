/*
 * vtk_preview.cpp
 *
 * path preview
 * (c) 2023 Robert Schöftner <rs@unfoo.net>
 */

#pragma once

#include "VtkViewer.h"

namespace ImCNC {

class VtkPreview
{
public:
  VtkPreview();

  void open_file(std::string path);
  void show();

private:
  VtkViewer m_viewer;
};

} // namespace ImCNC