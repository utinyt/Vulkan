#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inViewPos;
layout(location = 1) in vec3 inViewNormal;
layout(location = 0) out vec4 col;

#define LIGHT_NUM 20

struct Light{
	vec4 pos;
	vec3 color;
	float radius;
};

layout(binding = 1) uniform UBO {
	Light lights[LIGHT_NUM];
	int sampleCount;
} ubo;

vec3 CalculateLighting(vec3 pos, vec3 normal) {
	vec3 sum = vec3(0.f);

	//diffuse + spec
	for(int i = 0; i < LIGHT_NUM; ++i){
		float dist = length(ubo.lights[i].pos.xyz - pos);
//		if(dist > ubo.lights[i].radius)
//			continue;

		float att = ubo.lights[i].radius / (pow(dist, 2.0) + 1.0);

		//diffuse
		vec3 fragToLight = normalize(ubo.lights[i].pos.xyz - pos);
		float NL = max(dot(normal, fragToLight), 0.f);
		vec3 diffuse = att * NL * ubo.lights[i].color;

		//spec
		vec3 fragToCam = normalize(-pos);
		vec3 reflectDir = reflect(-fragToLight, normal);
		float spec = pow(max(dot(fragToCam, reflectDir), 0.0), 32);
		vec3 specular = att * spec * ubo.lights[i].color;

		sum += specular + diffuse;
	}

	//hardcoded ambient
	vec3 ambient = vec3(LIGHT_NUM * 0.001f);
	return sum + ambient;
}

void main(){
	//light calculation
	vec3 lighting = vec3(0.f);
	for(int i = 0; i < ubo.sampleCount; ++i){
		lighting += CalculateLighting(inViewPos, inViewNormal);
	}
	lighting /= float(ubo.sampleCount);

	col = vec4(lighting, 1.f);
}
