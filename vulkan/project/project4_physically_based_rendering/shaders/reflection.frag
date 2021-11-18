#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 viewNormal;
layout(location = 1) in vec3 viewFragPos;
layout(location = 0) out vec4 col;

layout(binding = 1) uniform samplerCube skybox;

void main(){
	vec3 camToFrag = normalize(viewFragPos);
	vec3 r = reflect(camToFrag, viewNormal);
	col = vec4(texture(skybox, r).xyz, 1.0);
}
