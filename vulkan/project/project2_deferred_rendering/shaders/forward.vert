#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UBO {
	mat4 view;
	mat4 normalMatrix;
	mat4 proj;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldTranslation;
layout(location = 3) in vec3 inScale;

layout(location = 0) out vec3 outViewPos;
layout(location = 1) out vec3 outViewNormal;

void main(){
	//construct model matrix based on the instances position input (inWorldTranslation)
	mat4 translation = mat4(1.f);
	//transformation
	translation[3][0] = inWorldTranslation.x;
	translation[3][1] = inWorldTranslation.y;
	translation[3][2] = inWorldTranslation.z;
	//scale
	mat4 scale = mat4(1.f);
	scale[0][0] = inScale.x;
	scale[1][1] = inScale.y;
	scale[2][2] = inScale.z;

	mat4 model = translation * scale;

	outViewPos = (ubo.view * model * vec4(inPos, 1.f)).xyz;
	gl_Position = ubo.proj * vec4(outViewPos, 1.f);
	outViewNormal = mat3(transpose(inverse(ubo.view * model))) * inNormal;
}