#include "pch.h"
#include "apps.h"
#include "timer.h"
#include "camera.h"
#include "editor.h"
#include "renderer.h"

int main(int argc, char** argv) {
    {
        auto app = Raekor::EditorOpenGL();

        double dt = 0;
        Raekor::Timer timer;

        while (app.running) {
            timer.start();
            app.update(dt);
            dt = timer.stop();
        }
    }

    return 0;
}