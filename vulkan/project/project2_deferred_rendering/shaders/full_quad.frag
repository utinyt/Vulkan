#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(set = 0, binding = 0) uniform sampler2DMS position;
layout(set = 0, binding = 1) uniform sampler2DMS normal;

#define LIGHT_NUM 6

struct Light{
	vec4 pos;
	vec3 color;
	float radius;
};

layout(set = 0, binding = 2) uniform UBO {
	Light lights[LIGHT_NUM];
	vec4 camPos;
	int renderMode;
	int sampleCount;
} ubo;

vec3 CalculateLighting(vec3 pos, vec3 normal) {
	vec3 sum = vec3(0.f);

	//diffuse + spec
	for(int i = 0; i < LIGHT_NUM; ++i){
		float dist = length(ubo.lights[i].pos.xyz - pos);
		float att = ubo.lights[i].radius / (pow(dist, 2.0) + 1.0);

		//diffuse
		vec3 fragToLight = normalize(ubo.lights[i].pos.xyz - pos);
		float NL = max(dot(normal, fragToLight), 0.f);
		vec3 diffuse = att * NL * ubo.lights[i].color;

		//spec
		vec3 fragToCam = normalize(ubo.camPos.xyz - pos);
		vec3 reflectDir = reflect(-fragToLight, normal);
		float spec = pow(max(dot(fragToCam, reflectDir), 0.0), 32);
		vec3 specular = att * spec * ubo.lights[i].color;

		sum += specular + diffuse;
	}

	//hardcoded ambient
	//vec3 ambient = vec3(LIGHT_NUM * 0.1f);
	return sum;
}

void main(){
	ivec2 UV = ivec2(textureSize(normal) * inUV);

	//debug render
	switch(ubo.renderMode){
	case 1: //position
		col = texelFetch(position, UV, 0); return;
	case 2: //normal
		col = vec4(texelFetch(normal, UV, 0).xyz, 1.f); return;
	}

	//light calculation
	vec3 lighting = vec3(0.f);
	for(int i = 0; i < ubo.sampleCount; ++i){
		vec4 samplePos = texelFetch(position, UV, i);
		vec3 pos = samplePos.xyz;
		vec3 normal = normalize(texelFetch(normal, UV, i).xyz);
		lighting += CalculateLighting(pos, normal) * samplePos.a;
	}
	lighting /= float(ubo.sampleCount);

	col = vec4(lighting, 1.f);
}
