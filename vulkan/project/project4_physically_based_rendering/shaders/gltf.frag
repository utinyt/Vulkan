#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#include "pbr.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 viewFragPos;
layout(location = 3) in vec3 viewLightPos;
layout(location = 0) out vec4 col;

layout(push_constant) uniform RasterPushConstant{
	mat4 modelMatrix;
	mat4 normalMatrix;
	vec3 lightPos;
	uint materialId;
};

layout(set = 0, binding = 2) uniform sampler2D textures[];

struct ShadeMaterial{
	vec4 baseColorFactor;
	vec3 emissiveFactor;
	int baseColorTextureIndex;
	float roughness;
	float metallic;
	float padding1;
	float padding2;
};

layout(set = 0, binding = 3) readonly buffer Materials {
	ShadeMaterial materials[];
};

void main(){
	ShadeMaterial material = materials[materialId];

	float roughness = max(material.roughness, 0.001);

	vec3 L = normalize(viewLightPos - viewFragPos);
	vec3 albedo = material.baseColorFactor.xyz;
	if(material.baseColorTextureIndex > -1){
		if(length(albedo) != 0)
			albedo *= texture(textures[material.baseColorTextureIndex], inUV).xyz;
		else
			albedo = texture(textures[material.baseColorTextureIndex], inUV).xyz;
	}
	vec3 V = normalize(-viewFragPos);
	float dist = length(viewLightPos - viewFragPos);

	//rendering equation
	vec3 Lo = BRDF(L, V, inNormal, material.metallic, roughness, albedo, 1);
	vec3 ambient = albedo * 0.02;

	vec3 outColor = Lo + ambient; //ambient
	outColor = outColor / (outColor + vec3(1.0)); //reinhard tonemapping
	outColor = pow(outColor, vec3(1.0 / 2.2)); //gamma correction

	col = vec4(outColor, 1.0);
}
