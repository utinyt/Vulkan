..\..\glslc.exe particle.vert -o particle_vert.spv -g
..\..\glslc.exe particle.frag -o particle_frag.spv -g
..\..\glslc.exe particle_compute.comp -o particle_compute_comp.spv -g
..\..\glslc.exe particle_update.comp -o particle_update_comp.spv -g
..\..\glslc.exe full_quad.vert -o full_quad_vert.spv -g
..\..\glslc.exe full_quad.frag -o full_quad_frag.spv -g
..\..\glslc.exe full_quad_extract_bright_color.frag -o full_quad_extract_bright_color_frag.spv -g
..\..\glslc.exe full_quad_bloom.frag -o full_quad_bloom_frag.spv -g
pause