#pragma once

#include "pch.h"

namespace Raekor {

class Camera {
    
public:
    Camera(glm::vec3 position, float fov);
    void look(int x, int y);
    void update();
    glm::vec3 get_direction();
    void move_on_input(double dt);
    void update(const glm::mat4& model);
    inline bool is_mouse_active() { return mouse_active; }
    inline void set_mouse_active(bool state) { mouse_active = state; }

    // getters
    inline glm::mat4 get_mvp(bool transpose) { return (transpose ? glm::transpose(mvp) : mvp); }
    inline float* get_move_speed() { return &move_speed; }
    inline float* get_look_speed() { return &look_speed; }

private:
    glm::vec3 position;
    glm::vec2 angle;
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 mvp;
    float FOV;
    float look_speed;
    float move_speed;
    bool mouse_active;

};

} // Namespace Raekor