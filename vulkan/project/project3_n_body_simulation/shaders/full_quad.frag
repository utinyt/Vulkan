#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(binding = 0) uniform sampler2D hdrImage;

void main(){
	vec3 hdrCol = texture(hdrImage, inUV).xyz;

	//reinhard tone mapping
	vec3 mapped = hdrCol / (hdrCol + vec3(1.f));
	//gamma correction
	mapped = pow(mapped, vec3(1.f / 2.2));

	col = vec4(mapped, 1.f);
}