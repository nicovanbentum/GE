# Raekor Engine

![image](https://i.imgur.com/nXhVK2H.png)

# How to build

### Windows
Use the supplied Visual Studio Project, binaries for SDL2 and Assimp are included.

### Linux (BROKEN SINCE VULKAN INTEGRATION)
enter `sudo apt-get install libsdl2-2.0` in a terminal.

Open a terminal and `cd` to `Raekor/dependencies/Assimp/`, do `cmake ./` then `make` then `sudo make install`.

`cd` to the Raekor directory and do `make rebuild`.

Run the application using `make run`.

>*Tested on Ubuntu 19.04*

### Render APIs
Most work is being put into abstsracting away OpenGL and DirectX. As of 2020 the project's focus has shifted towards Vulkan and getting more graphics features working using just Vulkan. Some of those features (in no particular order) are:
- [X] Shader hotloading
- [X] Directional & Point lighting
- [ ] Shadow mapping
- [ ] Deferred render pipeline
- [ ] Physically Based Rendering
- [ ] Screen Space Ambient Occlusion
- [ ] Screen Space Reflections
- [ ] Global Illumination (multiple techniques)

> More to follow.
