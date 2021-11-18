#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UBO {
	mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec3 outUVW;

void main(){
	outUVW = inPos;
	//outUVW.xy *= -1.0;
	mat4 viewNoTranslate = mat4(mat3(ubo.view));
	vec4 pos = ubo.proj * viewNoTranslate * vec4(inPos, 1.f);
	gl_Position = pos.xyww;
}