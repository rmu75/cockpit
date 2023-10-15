#
# Cross Platform Makefile
# Compatible with MSYS2/MINGW, Ubuntu 14.04.1 and Mac OS X
#
# You will need GLFW (http://www.glfw.org):
# Linux:
#   apt-get install libglfw-dev
# Mac OS X:
#   brew install glfw
# MSYS2:
#   pacman -S --noconfirm --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-glfw
#

#CXX = g++
#CXX = clang++

EXE = copilot
IMGUI_DIR = lib/imgui
IMGUI_VTK_DIR = lib/imgui-vtk
NODE_DIR = lib/imgui-node-editor
LINUXCNC_DIR = ../linuxcnc
COLOR_TEXT_EDIT_DIR = lib/imgui-color-text-edit
SOURCES = src/main.cpp src/imcnc.cpp src/imhal.cpp src/shcom.cpp src/vtk_preview.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
SOURCES += $(IMGUI_VTK_DIR)/VtkViewer.cpp
SOURCES += $(IMGUI_VTK_DIR)/gl3w/src/gl3w.c
SOURCES += $(NODE_DIR)/crude_json.cpp $(NODE_DIR)/imgui_canvas.cpp $(NODE_DIR)/imgui_node_editor_api.cpp $(NODE_DIR)/imgui_node_editor.cpp
SOURCES += $(COLOR_TEXT_EDIT_DIR)/TextEditor.cpp

OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))
UNAME_S := $(shell uname -s)
LINUX_GL_LIBS = -lGL

CXXFLAGS = -std=c++20 -Iinclude -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(LINUXCNC_DIR)/include
CXXFLAGS += -I$(IMGUI_VTK_DIR) -I$(NODE_DIR) -I$(COLOR_TEXT_EDIT_DIR)
CXXFLAGS += -I/usr/include/vtk-9.1
CXXFLAGS += -I$(IMGUI_VTK_DIR)/gl3w/include
CXXFLAGS += -O2 -g -Wall -Wformat -DULAPI
LIBS = -L$(LINUXCNC_DIR)/lib -lnml -llinuxcnchal -llinuxcnc -llinuxcncini -ltirpc

CXXFLAGS += -DvtkRenderingCore_AUTOINIT="3(vtkInteractionStyle,vtkRenderingFreeType,vtkRenderingOpenGL2)" -DvtkRenderingOpenGL2_AUTOINIT="1(vtkRenderingGL2PSOpenGL2)"

##---------------------------------------------------------------------
## OPENGL ES
##---------------------------------------------------------------------

## This assumes a GL ES library available in the system, e.g. libGLESv2.so
# CXXFLAGS += -DIMGUI_IMPL_OPENGL_ES2
# LINUX_GL_LIBS = -lGLESv2

##---------------------------------------------------------------------
## BUILD FLAGS PER PLATFORM
##---------------------------------------------------------------------

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += $(LINUX_GL_LIBS) `pkg-config --static --libs glfw3`

	CXXFLAGS += `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
	LIBS += -L/usr/local/lib -L/opt/local/lib -L/opt/homebrew/lib
	#LIBS += -lglfw3
	LIBS += -lglfw

	CXXFLAGS += -I/usr/local/include -I/opt/local/include -I/opt/homebrew/include
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(OS), Windows_NT)
	ECHO_MESSAGE = "MinGW"
	LIBS += -lglfw3 -lgdi32 -lopengl32 -limm32

	CXXFLAGS += `pkg-config --cflags glfw3`
	CFLAGS = $(CXXFLAGS)
endif

LIBS += \
	/usr/lib/x86_64-linux-gnu/libvtkRenderingAnnotation-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkInteractionStyle-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkInteractionWidgets-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkRenderingFreeType-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkIOImport-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libfreetype.so \
	/usr/lib/x86_64-linux-gnu/libvtkRenderingGL2PSOpenGL2-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkRenderingOpenGL2-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkRenderingUI-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkRenderingCore-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkCommonColor-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkFiltersSources-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkFiltersGeneral-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkFiltersParallel-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkFiltersCore-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkCommonExecutionModel-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkCommonDataModel-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkCommonTransforms-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkCommonMisc-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkCommonMath-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libvtkkissfft-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libGLEW.so \
	/usr/lib/x86_64-linux-gnu/libX11.so \
	/usr/lib/x86_64-linux-gnu/libvtkCommonCore-9.1.so.9.1.0 \
	/usr/lib/x86_64-linux-gnu/libtbb.so.12.5 \
	/usr/lib/x86_64-linux-gnu/libvtksys-9.1.so.9.1.0

##---------------------------------------------------------------------
## BUILD RULES
##---------------------------------------------------------------------

%.o:src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(NODE_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_VTK_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_VTK_DIR)/gl3w/src/%.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(COLOR_TEXT_EDIT_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

all: $(EXE)
	@echo Build complete for $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

clean:
	rm -f $(EXE) $(OBJS)
