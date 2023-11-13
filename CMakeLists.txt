cmake_minimum_required(VERSION 3.20.0)
project(cockpit VERSION 0.1.0 LANGUAGES C CXX)

# include(CTest)
# enable_testing()
set(EXEC_NAME cockpit)

file(GLOB cockpit_src src/*.cpp)
add_executable(${EXEC_NAME} ${cockpit_src})
target_include_directories(cockpit PUBLIC include/)
target_compile_definitions(cockpit PUBLIC ULAPI)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
set(CMAKE_BUILD_TYPE debug)
set(GLFW_BUILD_WAYLAND ON)

#add_subdirectory(src)

# set(CPACK_PROJECT_NAME ${PROJECT_NAME})
# set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
# include(CPack)

# OpenGL
find_package(OpenGL REQUIRED)
target_link_libraries(${EXEC_NAME} OpenGL::GL)
if (APPLE)
        # Ignore macOS OpenGL deprecation warnings
        target_compile_definitions(${EXEC_NAME} PRIVATE GL_SILENCE_DEPRECATION)
endif()

# OpenGL Loader - GL3W
# This can be replaced with your own OpenGL Loader
# Either compile it as a static library like this example
# Or add it to the source files for the main executable + imgui + vtkviewer
set(gl3w_dir ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui-vtk/gl3w)
set(gl3w_inc ${gl3w_dir}/include)
set(gl3w_src ${gl3w_dir}/src/gl3w.c)
if (EXISTS ${gl3w_src})
        message(STATUS "Found gl3w source files")
else()
        execute_process(COMMAND python3 gl3w_gen.py WORKING_DIRECTORY ${gl3w_dir}) # use python or python3
        if (NOT EXISTS ${gl3w_src})
                message(FATAL_ERROR "Could not generate gl3w source files")
        endif()
endif()
add_library(gl3w STATIC ${gl3w_src})
target_include_directories(gl3w PUBLIC ${gl3w_inc})
target_link_libraries(${EXEC_NAME} gl3w)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui-vtk/glfw)
target_link_libraries(${EXEC_NAME} glfw)

# Dear ImGui
set(imgui_dir ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui)
file(GLOB imgui_src ${imgui_dir}/*.cpp)
set(imgui_backend ${imgui_dir}/backends/imgui_impl_glfw.cpp ${imgui_dir}/backends/imgui_impl_opengl3.cpp)
add_library(imgui STATIC ${imgui_src} ${imgui_backend})
target_include_directories(imgui PUBLIC ${imgui_dir} ${imgui_dir}/backends)
target_link_libraries(imgui glfw)
target_link_libraries(${EXEC_NAME} imgui)
target_include_directories(${EXEC_NAME} PUBLIC ${imgui_dir} ${imgui_dir}/backends)

# imgui color text editor
set(imgui_color_text_edit_dir ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui-color-text-edit)
add_library(imgui_color_text_editor STATIC ${imgui_color_text_edit_dir}/TextEditor.cpp)
target_link_libraries(${EXEC_NAME} imgui_color_text_editor)
target_include_directories(imgui_color_text_editor PUBLIC ${imgui_dir})
target_include_directories(${EXEC_NAME} PUBLIC ${imgui_color_text_edit_dir})

# imgui node editor
# tbd

# linuxcnc
set(linuxcnc_dir ${CMAKE_CURRENT_SOURCE_DIR}/../linuxcnc-rs-zmq-experiments)
target_include_directories(cockpit PUBLIC ${linuxcnc_dir}/include)
target_link_directories(cockpit PUBLIC ${linuxcnc_dir}/lib)
target_link_libraries(cockpit nml linuxcnchal linuxcnc linuxcncini tirpc)

# VTK
find_package(VTK COMPONENTS 
  CommonCore
  CommonColor
  CommonDataModel
  FiltersCore
  InteractionStyle
  InteractionWidgets
  RenderingAnnotation
  RenderingCore
  RenderingFreeType
  RenderingGL2PSOpenGL2
  RenderingOpenGL2
  QUIET
)
if (NOT VTK_FOUND)
  message(FATAL_ERROR "VTK not found!")
  return()
endif()

if (VTK_VERSION VERSION_LESS "9.0.0")
  include(${VTK_USE_FILE})
else()
  # vtk_module_autoinit is needed
  vtk_module_autoinit(
    TARGETS ${EXEC_NAME}
    MODULES ${VTK_LIBRARIES}
  )
endif()
message (STATUS "VTK Version: ${VTK_VERSION}")

# imgui-vtk (VTK Viewer class)
set(imgui_vtk_viewer_dir ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui-vtk)
add_library(imgui_vtk_viewer STATIC ${imgui_vtk_viewer_dir}/VtkViewer.cpp)
target_include_directories(imgui_vtk_viewer PUBLIC ${imgui_vtk_viewer_dir})
target_include_directories(cockpit PUBLIC ${imgui_vtk_viewer_dir})
target_link_libraries(imgui_vtk_viewer gl3w) # Since gl3w was compiled as a static library, we need to link to it
target_link_libraries(imgui_vtk_viewer imgui) # Since imgui was compiled as a static library, we need to link to it
target_link_libraries(imgui_vtk_viewer ${VTK_LIBRARIES})
target_link_libraries(${EXEC_NAME} imgui_vtk_viewer)

# flatbuffers
find_package(Flatbuffers)

# 0mq
#find_package(zmq)

target_link_libraries(${EXEC_NAME} zmq zmqpp)