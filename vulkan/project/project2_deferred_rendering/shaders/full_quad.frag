#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(set = 0, binding = 0) uniform sampler2DMS position;
layout(set = 0, binding = 1) uniform sampler2DMS normal;

#define LIGHT_NUM 20

struct Light{
	vec4 pos;
	vec3 color;
	float radius;
};

layout(set = 0, binding = 2) uniform UBO {
	Light lights[LIGHT_NUM];
	int renderMode;
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
	ivec2 UV = ivec2(textureSize(normal) * inUV);
	col = vec4(1.f);

	//debug render
	switch(ubo.renderMode){
	case 1: //position
	
		col = texelFetch(position, UV, 0);
//		vec3 center = texelFetch(normal, UV, 0).xyz;
//		vec3 top = texelFetch(normal, UV + ivec2(0, 1), 0).xyz; 
//		vec3 left = texelFetch(normal, UV + ivec2(-1, 0), 0).xyz; 
//		vec3 right = texelFetch(normal, UV + ivec2(1, 0), 0).xyz; 
//		vec3 bottom = texelFetch(normal, UV + ivec2(0, -1), 0).xyz;
//
//		float normalDiffTop = length(center - top);
//		float normalDiffLeft = length(center - left);
//		float normalDiffRight = length(center - right);
//		float normalDiffBottom = length(center - bottom);
//
//		float threshold = 0.5f;
//
//		if(normalDiffTop < threshold &&
//			normalDiffLeft < threshold &&
//			normalDiffRight < threshold &&
//			normalDiffBottom < threshold){
//			col = vec4(1.f, 0.f, 0.f, 1.f); //red - not edge
//		}
//		else{
//			col = vec4(0.f, 0.f, 1.f, 1.f); //blue - edge
//		}
		return;

	
		

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

	
	if(gl_FragCoord.x < 200)
		col = vec4(lighting, 1.f);
}
