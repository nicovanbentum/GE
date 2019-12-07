#include "pch.h"
#include "camera.h"

namespace Raekor {

Camera::Camera(glm::vec3 position, float fov) :
    position(position),
    angle(static_cast<float>(M_PI), 0.0f), 
    FOV(fov), look_speed(0.0010f), move_speed(0.05f),
    mouse_active(true) {
    projection = glm::perspectiveRH_ZO(glm::radians(FOV), 16.0f/9.0f, 0.1f, 10000.0f);
}

void Camera::update() {
    auto dir = get_direction();
    view = glm::lookAtRH(position, position + dir, {0, 1, 0});
}

void Camera::set_aspect_ratio(float new_ratio) {
    projection = glm::perspectiveRH_ZO(glm::radians(FOV), new_ratio, 0.1f, 10000.0f);
}

glm::vec3 Camera::get_direction() {
    return glm::vec3(cos(angle.y) * sin(angle.x), 
    sin(angle.y), cos(angle.y) * cos(angle.x));
}
    
void Camera::look(int x, int y) {
    angle.x += look_speed * (x * -1);
    angle.y += look_speed * (y * -1);
    angle.y = std::clamp(angle.y, -1.5f, 1.5f);
}

void Camera::move_on_input(double dt) {
    auto dir = get_direction();
    const uint8_t* keyboard = SDL_GetKeyboardState(NULL);
    if (keyboard[SDL_SCANCODE_W]) {
        position += dir * (float)(move_speed*dt);
    } 
    else if (keyboard[SDL_SCANCODE_S]) {
            position -= dir * (float)(move_speed*dt);
    }
    if (keyboard[SDL_SCANCODE_A]) {
        position -= glm::normalize(glm::cross(dir, { 0,1,0 })) * (float)(move_speed*dt);
    }
    else if (keyboard[SDL_SCANCODE_D]) {
        position += glm::normalize(glm::cross(dir, { 0,1,0 })) * (float)(move_speed*dt);
    }
}

}
