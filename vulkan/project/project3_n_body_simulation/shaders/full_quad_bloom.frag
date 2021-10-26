#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 col;

layout(binding = 0) uniform sampler2D brightImage;

layout(constant_id = 0) const int horizontal = 0;

void main(){
	float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
	vec2 texOffset = 1.0 / textureSize(brightImage, 0);
	vec3 result = texture(brightImage, inUV).xyz * weight[0];
	
	if(horizontal == 1){
		for(int i = 1; i < 5; ++i){
			result += texture(brightImage, inUV + vec2(texOffset.x  * i, 0.0)).xyz * weight[i];
			result += texture(brightImage, inUV - vec2(texOffset.x  * i, 0.0)).xyz * weight[i];
		}
	}
	else{
		for(int i = 1; i < 5; ++i){
			result += texture(brightImage, inUV + vec2(0.0, texOffset.y  * i)).xyz * weight[i];
			result += texture(brightImage, inUV - vec2(0.0, texOffset.y  * i)).xyz * weight[i];
		}
	}
	
	col = vec4(result, 1.f);
}
