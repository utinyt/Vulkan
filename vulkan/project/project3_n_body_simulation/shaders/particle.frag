#version 450

layout(location = 0) out vec4 col;

layout(binding = 1) uniform sampler2D particleTex;

void main(){
	col.xyz = texture(particleTex, gl_PointCoord).xyz;
	col.xyz *= vec3(0.5f, 0.8f, 1.f);
}
