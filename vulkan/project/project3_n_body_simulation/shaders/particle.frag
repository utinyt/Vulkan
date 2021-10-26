#version 450

layout(location = 0) out vec4 col;

layout(binding = 1) uniform sampler2D particleTex;

void main(){
	vec3 hdrCol = texture(particleTex, gl_PointCoord).xyz;
	col.xyz = hdrCol.xyz; // blue tint
	col.xyz *= vec3(0.3f, 0.3f, 1.f);
}
