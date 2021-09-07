#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UBO {
	mat4 model;
	mat4 view;
	mat4 proj;
} ubo;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 viewFragPos;
layout(location = 2) out vec3 lightPos;

void main(){
	mat4 modelView = ubo.view * ubo.model;
	viewFragPos = (modelView * vec4(inPos, 1.f)).xyz;
	gl_Position = ubo.proj * vec4(viewFragPos , 1.f);
	normal = mat3(transpose(inverse(modelView))) * inNormal;
	lightPos = (ubo.view * vec4(-2.f, 2.f, 2.f, 1.f)).xyz;
}