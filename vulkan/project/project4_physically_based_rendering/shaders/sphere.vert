#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UBO {
	mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
	vec4 camPos;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec3 outPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 camPos;


layout(push_constant) uniform RasterPushConstant{
	mat4 modelMatrix;
	mat4 normalMatrix;
	float metallic;
	float roughness;
	uint materialId;
	float padding;
};

void main(){
	vec4 modelPos = modelMatrix * vec4(inPos, 1.f);
	gl_Position = ubo.proj * ubo.view * modelPos;
	outPos = modelPos.xyz;
	outNormal = inNormal;
	outUV = inUV;
	camPos = ubo.camPos.xyz;
}