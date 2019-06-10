#pragma once

#include "pch.h"

namespace Raekor {

class Camera {
    
public:
    glm::vec3 position;
    float angle_h;
    float angle_v;
    float FOV;
    float look_speed;
    bool mouse_active;
    glm::mat4 projection;
    glm::mat4 view;

    Camera(glm::vec3 position, float fov);
    void update();
    void add_look(int x, int y);
    void move_on_input(float amount);
    bool is_mouse_active();
    void set_mouse_active(bool state);
    glm::vec3 get_direction();

};
}