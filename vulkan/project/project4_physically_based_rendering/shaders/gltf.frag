#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

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
};

layout(set = 0, binding = 3) readonly buffer Materials {
	ShadeMaterial materials[];
};

void main(){
	ShadeMaterial material = materials[materialId];

	//diffuse color
	vec3 fragToLight = viewLightPos - viewFragPos;
	fragToLight = normalize(fragToLight);
	float diff = max(dot(fragToLight, inNormal), 0);
	col = vec4(vec3(diff) * material.baseColorFactor.xyz, 1.f);
	if(material.baseColorTextureIndex > -1){
		col.xyz *= texture(textures[material.baseColorTextureIndex], inUV).xyz;
	}

	//specular
	vec3 fragToView = normalize(-viewFragPos);
	vec3 reflectDir = reflect(-fragToLight, inNormal);
	float spec = 5 * pow(max(dot(fragToView, reflectDir), 0), 64);

	col.xyz += vec3(spec);
}
