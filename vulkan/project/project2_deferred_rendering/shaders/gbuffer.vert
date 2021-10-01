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

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

void main(){
	//construct model matrix based on the instances position input (inWorldTranslation)
	mat4 model = mat4(1.f);
	model[3][0] = inWorldTranslation.x;
	model[3][1] = inWorldTranslation.y;
	model[3][2] = inWorldTranslation.z;
	
	outWorldPos = (model * vec4(inPos, 1.f)).xyz;
	gl_Position = ubo.proj * ubo.view * vec4(outWorldPos , 1.f);
	outWorldNormal = mat3(transpose(inverse(model))) * inNormal;
}