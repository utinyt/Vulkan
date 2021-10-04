#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inViewPos;
layout(location = 1) in vec3 inViewNormal;
layout(location = 0) out vec4 pos;
layout(location = 1) out vec4 normal;

void main(){
	pos = vec4(inViewPos, 1.f);
	normal = vec4(inViewNormal, 1.f);
}
