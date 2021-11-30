..\..\glslc.exe phong.vert -o phong_vert.spv
..\..\glslc.exe phong.frag -o phong_frag.spv
..\..\glslc.exe skydome.vert -o skydome_vert.spv
..\..\glslc.exe skydome.frag -o skydome_frag.spv
..\..\glslc.exe sphere.vert -o sphere_vert.spv
..\..\glslc.exe sphere.frag -o sphere_frag.spv
..\..\glslc.exe reflection.vert -o reflection_vert.spv
..\..\glslc.exe reflection.frag -o reflection_frag.spv
..\..\glslc.exe gltf.vert -o gltf_vert.spv --target-env=vulkan1.2
..\..\glslc.exe gltf.frag -o gltf_frag.spv --target-env=vulkan1.2
pause