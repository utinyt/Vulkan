#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#include "pbr.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 camPos;
layout(location = 0) out vec4 col;

layout(binding = 1) uniform sampler2D skydome;

layout(push_constant) uniform RasterPushConstant{
	mat4 modelMatrix;
	mat4 normalMatrix;
	float metallic;
	float roughness;
	uint materialId;
	float padding;
};

vec2 convertVectorToUV(vec3 r){
	const float PI = 3.141592;
	float phi = acos(r.y) / PI;
	float theta;
	if(r.z < 0) {
		if(r.x > 0){ //1st
			theta = 1 - (atan(-r.z / r.x)) / (2 * PI);
		}
		else{ //2th
			theta = 1 - (atan(-r.z / r.x) + PI) / (2 * PI);	
		}
	}
	else{
		if(r.x > 0){//4nd
			theta = (atan(r.z / r.x)) / (2 * PI);
		}
		else {//3rd
			theta = (atan(r.z / r.x) + PI) / (2 * PI);
		}
	}
	return vec2(theta, phi);
}

void main(){
	vec3 r = normalize(reflect(normalize(inPos - camPos), inNormal));
	vec2 uv = convertVectorToUV(r);
		
	vec3 envCol = texture(skydome, uv).xyz;
	envCol = envCol  / (envCol + vec3(1.f));
	envCol  = pow(envCol, vec3(1.0 / 2.2));

	vec3 lightPos = vec3(-10, 20, 10);
	vec3 L = normalize(lightPos - inPos);
	vec3 albedo = vec3(1, 0, 0);
	
	vec3 V = normalize(camPos - inPos);
	float dist = length(lightPos - inPos);

	//rendering equation
	vec3 Lo = BRDF(L, V, inNormal, metallic, roughness, albedo, 1);
	vec3 ambient = albedo * 0.02;

	vec3 sum = Lo + ambient;
	sum  = sum / (sum + vec3(1.0));
	sum = pow(sum, vec3(1.0 / 2.2));

	col = vec4(sum, 1.0);
}
