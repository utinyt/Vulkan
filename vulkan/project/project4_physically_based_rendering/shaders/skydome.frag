#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(binding = 1) uniform sampler2D skydome;

void main(){
	vec3 envCol = texture(skydome, inUV).xyz;
	envCol = envCol  / (envCol + vec3(1.f));
	envCol  = pow(envCol, vec3(1.0 / 2.2));
	col = vec4(envCol, 1.f);
}
