#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 proj;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 outUVW;

void main(){
	outUVW = inPos;
	outUVW.xy *= -1.0;
	vec4 pos = ubo.proj * ubo.view * vec4(inPos, 1.f);
	gl_Position = pos.xyww;
}