#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UBO {
	mat4 view;
	mat4 proj;
} ubo;

layout(location = 0) in vec4 inPosMass;
layout(location = 1) in vec4 inVel;

void main(){
	gl_Position = ubo.proj * ubo.view * vec4(inPosMass.xyz, 1.f);
	gl_PointSize = 1.f;
}