#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(binding = 0) uniform sampler2D hdrImage;

void main(){
	vec4 hdrCol = texture(hdrImage, inUV);

	float brightness = dot(hdrCol.xyz, vec3(0.2126, 0.7152, 0.0722));
	
	if(brightness > 1.f){
		col = hdrCol;
	}
	else{
		col = vec4(0.f, 0.f, 0.f, 1.f);
	}
}
