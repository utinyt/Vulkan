#version 460
#extension GL_GOOGLE_include_directive : enable

layout(binding = 0, set = 0) uniform CameraMatrices{
    mat4 view;
    mat4 proj;
    mat4 viewInverse;
    mat4 projInverse;
} cam;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 viewFragPos;
layout(location = 3) out vec3 viewLightPos;

layout(push_constant) uniform RasterPushConstant{
	mat4 modelMatrix;
	mat4 normalMatrix;
	float metallic;
	float roughness;
	uint materialId;
	float padding;
};

void main(){
	mat4 modelView = cam.view * modelMatrix;
	viewFragPos = (modelView * vec4(inPos, 1.f)).xyz;
	gl_Position = cam.proj * vec4(viewFragPos , 1.f);
	outNormal = normalize(mat3(transpose(inverse(modelView))) * inNormal);
	//viewLightPos = (cam.view * vec4(lightPos, 1.f)).xyz;
	outUV = inUV;
}