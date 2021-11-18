..\..\glslc.exe phong.vert -o phong_vert.spv
..\..\glslc.exe phong.frag -o phong_frag.spv
..\..\glslc.exe skybox.vert -o skybox_vert.spv
..\..\glslc.exe skybox.frag -o skybox_frag.spv
..\..\glslc.exe reflection.vert -o reflection_vert.spv
..\..\glslc.exe reflection.frag -o reflection_frag.spv
..\..\glslc.exe gltf.vert -o gltf_vert.spv --target-env=vulkan1.2
..\..\glslc.exe gltf.frag -o gltf_frag.spv --target-env=vulkan1.2
pause