#version 450

layout(location = 0) out vec4 col;

layout(binding = 1) uniform sampler2D particleTex;

void main(){
	col = texture(particleTex, gl_PointCoord);
	col.xyz *= vec3(0.5f, 0.8f, 1.f);
}
