#version 450
#extension GL_ARB_separate_shader_objects : enable

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

void main(){
	const float PI = 3.141592;
	vec3 r = normalize(reflect(normalize(inPos - camPos), inNormal));
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
		
	vec3 envCol = texture(skydome, vec2(theta, phi)).xyz;
	envCol = envCol  / (envCol + vec3(1.f));
	envCol  = pow(envCol, vec3(1.0 / 2.2));
	col = vec4(envCol, 1.0);
}
