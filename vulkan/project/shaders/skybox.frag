#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inUVW;
layout(location = 0) out vec4 col;

layout(binding = 1) uniform samplerCube skybox;

void main(){
	col = texture(skybox, inUVW);
}
