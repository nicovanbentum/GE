# Raekor Engine

![image](https://i.imgur.com/31qDNlr.png)

# Build

## Windows
This project relies on [VCPKG](https://github.com/microsoft/vcpkg) to build the big binaries, make sure to have it installed and added to your PATH ([help](https://www.architectryan.com/2018/03/17/add-to-the-path-on-windows-10/)) so the build script can automatically find the executable. 

Next double click the ```init.bat``` file in Raekor's main directory and let it do its thing (can take a while to compile Assimp and Bullet3).
Once that is done open up the supplied Visual Studio 2019 project. From there pick a configuration like Release (it's all 64 bit) and run/build. 



## Linux
Used to work through the supplied makefile, but it's out of date.

# Features
Some experimentals are in place for DirectX 11 and Vulkan. Focus has shifted to getting features in fast in OpenGL so I can use the engine to  research and implement Global Illumination for a research course at the Hogeschool Utrecht. Some of the features currently being worked on:
- [X] Shader hotloading
- [X] Directional & Point lighting
- [X] Shadow mapping (point & directional)
- [X] Normal mapping
- [X] HDR, Gamma & Tone mapping
- [X] Deferred render pipeline
- [ ] PBR Material system
- [X] Screen Space Ambient Occlusion
- [ ] Screen Space Reflections
- [ ] Global Illumination (multiple techniques)
- [ ] Scripting language
- [x] Data driven Entity Component System

> More to follow.
