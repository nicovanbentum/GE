#include "pch.h"
#include "renderer.h"

#ifdef _WIN32
    #include "DXRenderer.h"
#endif

namespace Raekor {

static void log_msg(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION) {
        std::cout << message << std::endl;
    }
}

bool Renderer::set_activeAPI(const RenderAPI new_api) {
#ifdef _WIN32
    if(activeAPI != new_api) {
        activeAPI = new_api;
        return true;
    }
#endif
// if we're not on linux we just never change API
// TODO: subject to change if Vulkan is ever implemented
    return false;
}

// global enum variable for changing the active render API
RenderAPI Renderer::activeAPI = RenderAPI::OPENGL;
std::unique_ptr<Renderer> Render::renderer;

Renderer* Renderer::construct(SDL_Window* window) {
    switch (activeAPI) {
        case RenderAPI::OPENGL: {
            return new GLRenderer(window);
        } break;
#ifdef _WIN32
        case RenderAPI::DIRECTX11: {
            return new DXRenderer(window);
        } break;
#endif
    }
    return nullptr;
}

GLRenderer::GLRenderer(SDL_Window* window) {
    render_window = window;
    
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
    
    context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, context);
    int gl3w_err = gl3wInit();
    m_assert(gl3w_err == 0, "failed to init gl3w");

    SDL_GL_SetSwapInterval(0);
    
    // print the initialized openGL specs
    std::cout << "Active Rendering API: OpenGL " << glGetString(GL_VERSION);
    printf("GL INFO: OpenGL %s, GLSL %s\n", glGetString(GL_VERSION),
        glGetString(GL_SHADING_LANGUAGE_VERSION));
    
    ImGui_ImplSDL2_InitForOpenGL(window, &context);
    ImGui_ImplOpenGL3_Init("#version 330");

    glDebugMessageCallback(log_msg, nullptr);

    // set opengl depth testing and culling
    //glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // one time setup for binding vertey arrays
    unsigned int vertex_array_id;
    glGenVertexArrays(1, &vertex_array_id);
    glBindVertexArray(vertex_array_id);
}

GLRenderer::~GLRenderer() {
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    SDL_GL_DeleteContext(context);
}

void GLRenderer::Clear(glm::vec4 color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLRenderer::ImGui_NewFrame(SDL_Window* window) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();
}

void GLRenderer::ImGui_Render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GLRenderer::SwapBuffers(bool vsync) const {
    SDL_GL_SetSwapInterval(vsync);
    SDL_GL_SwapWindow(render_window);
}

void GLRenderer::DrawIndexed(unsigned int size, bool depth_test) {
    if (!depth_test) glDepthMask(false);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDrawElements(GL_TRIANGLES, size, GL_UNSIGNED_INT, nullptr);
    glDisable(GL_DEPTH_TEST);
    if (!depth_test) glDepthMask(true);
}

} // namespace Raekor