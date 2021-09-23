# DigiPen GAM400 - Vulkan Technical Demo
This is a realtime 3D rendering application using Vulkan / C++ / GLSL.

## Developing Environments
* OS: Windows 10
* IDE: Visual Studio 2019
* GPU: RTX 2060 SUPER

Successful build on other environments is not guaranteed.

## Third Party
* [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)
* [stb collection](https://github.com/nothings/stb)
* [imgui](https://github.com/ocornut/imgui)

## References
A lot of code were based on these great resources:
* [Vulkan tutorial by Alexander Overvoorde](https://vulkan-tutorial.com/Introduction)
* [Vulkan samples by Sascha Willems](https://github.com/SaschaWillems/Vulkan)
* [Nvpro Core from NVIDIA DesignWorks Samples](https://github.com/nvpro-samples/nvpro_core)

## Updates
### MSAA - 21/09/16
![mass_x1](https://github.com/jooho556/TeamPositive/blob/master/vulkan/screenshots/msaa_x1.png)<br>
![mass_x8](https://github.com/jooho556/TeamPositive/blob/master/vulkan/screenshots/msaa_x8.png)<br>
Note: It is not recommended to change sample count dynamically (like above) as it results lots of vulkan resource recreation.

------

### Skybox & environment (reflection) mapping - 21/09/16
![skybox](https://github.com/jooho556/TeamPositive/blob/master/vulkan/screenshots/skybox.gif)<br>
Used skybox textures from: http://www.humus.name/index.php?page=Textures&start=8

------

### Initial framework - 21/09/06
![initial_framework](https://github.com/jooho556/TeamPositive/blob/master/vulkan/screenshots/initial_framework.png)<br>
<br>
Initial scene with stanford bunny - simple diffuse light applied <br>

Most of basic vulkan features were implemented at this point: <br>
Devices           - Collection of physical / logical device handle <br>
Swapchain         - Manage images in swapchain (acquire / present) <br>
MemoryAllocator   - Naive memory allocator that pre-allocate big chunk(s) of memory per memory type <br>
Texture2D         - Load texture from a file (stb_image used) <br>
Mesh              - Load model from a obj file (tinyobjloader used) <br>
