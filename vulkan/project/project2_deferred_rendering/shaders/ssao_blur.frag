#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out float col;

layout(binding = 0) uniform sampler2DMS ssaoOcclusion;

void main(){
	ivec2 UV = ivec2(inUV * textureSize(ssaoOcclusion));
	float result = 0.f;
	for(int x = -2; x < 2; ++x){
		for(int y = -2; y < 2; ++y){
			ivec2 offset = ivec2(x, y);
			for(int i = 0; i < 8; ++i)
				result += texelFetch(ssaoOcclusion, UV  + offset, i).x;
		}
	}
	col = result / (16.f * 8);
}
