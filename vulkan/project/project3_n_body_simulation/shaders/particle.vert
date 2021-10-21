#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 inPosMass;
layout(location = 1) in vec4 inVel;

layout(binding = 0) uniform UBO {
	mat4 view;
	mat4 proj;
} ubo;

void main(){
	float spriteSize = 0.005f * inPosMass.w;
	vec4 viewPos = ubo.view * vec4(inPosMass.xyz, 1.f);
	vec4 projectedCorner = ubo.proj * vec4(0.5 * spriteSize, 0.5 * spriteSize, viewPos.z, viewPos.w);
	gl_PointSize = clamp(1200 * projectedCorner.x / projectedCorner.w, 1.f, 128.f);
	gl_Position = ubo.proj * viewPos;
}