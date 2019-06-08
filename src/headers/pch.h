#pragma once

// platform specific includes
#include "GL/gl3w.h"

#ifdef _WIN32
    #include <windows.h>
	#include <commdlg.h>
    #include <GL/GL.h>

#elif __linux__
    #include <GL/gl.h>
    #include <gtk/gtk.h>
    
#endif

//openGL includes

// single include header only JSON library
#include "nlohmann/json.hpp" 
using json = nlohmann::json;

// imgui headers to build once
#include "imconfig.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imstb_rectpack.h"
#include "imstb_textedit.h"
#include "imstb_truetype.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl.h"

// openGL math library
#include "glm.hpp"
#include "ext.hpp"
#include "gtx/quaternion.hpp"
using namespace glm;

// SDL includes
#include "SDL.h"
#undef main //stupid sdl_main

// c++ includes
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>


