#version 450
//#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 inUV;
layout(location = 0) out float col;

layout(binding = 0) uniform sampler2DMS inPosition;
layout(binding = 1) uniform sampler2DMS inNormal;
layout(binding = 2) uniform sampler2D ssaoNoise;

layout(binding = 3) uniform UBOSampleKernel{
	vec4 samples[64];
} uboSampleKernel;

layout(binding = 4) uniform UBOCamera{
	mat4 view;
	mat4 normalMatrix;
	mat4 proj;
} uboCamera;

void main(){
	vec2 noiseScale = vec2(textureSize(inNormal) / 4.f);
	ivec2 UV = ivec2(textureSize(inNormal) * inUV);

	//this attachment will be blurred - no need to multisample
	vec3 fragPos = texelFetch(inPosition, UV, 0).xyz;
	vec3 normal = texelFetch(inNormal, UV, 0).xyz;

	vec3 randomVec = texture(ssaoNoise, inUV).xyz;
	vec3 tangent = normalize(randomVec - normal * dot(normal, randomVec)); //Gramm-Schmidt process
	vec3 bitangent = cross(normal, tangent);
	mat3 TBN = mat3(tangent, bitangent, normal);

	float occlusion = 0.f;
	float radius = 0.5f;
	float bias = 0.025f;
	for(int i = 0; i < 64; ++i){
		vec3 samplePos = TBN * uboSampleKernel.samples[i].xyz;
		samplePos = fragPos + samplePos * radius;

		vec4 offset = vec4(samplePos, 1.f);
		offset = uboCamera.proj * offset;
		offset.xyz /= offset.w;
		offset.xyz = offset.xyz * 0.5f + 0.5f;

		float sampleDepth = texelFetch(inPosition, ivec2(offset.xy * textureSize(inPosition)), 0).z; //real depth value
		float rangeCheck = smoothstep(0.f, 1.f, radius / abs(fragPos.z - sampleDepth));
		occlusion += (sampleDepth >= samplePos.z + bias ? 1.f : 0.f) * rangeCheck;
	}
	occlusion = 1.f - (occlusion / 64.f);
	col = occlusion;
}
