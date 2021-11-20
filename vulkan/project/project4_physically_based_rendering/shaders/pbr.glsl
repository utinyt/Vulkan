
//f term - approximate how much light is reflected (the ratio of light reflected / refracted)
vec3 fresnelSchlick(float hDotV, vec3 albedo, vec3 F0, float metallic){
	F0 = mix(vec3(F0), albedo, metallic);
	return F0 + (1.0 - F0) * pow(clamp(1.0 - hDotV, 0.0, 1.0), 5.0);
}

//d term - approximate how much microfacets are aligned to halfway
float distributionGGX(float nDotH, float roughness) {
	float PI = 3.141592;
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = (nDotH  * nDotH * (alpha2 - 1.0) + 1.0);
	return alpha2 / (PI * denom * denom);
}

//g term
float geometrySchlickGGX(float nDotL, float nDotV, float roughness) {
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	float gl = nDotL / (nDotL * (1.0 - k) + k);
	float gv = nDotV / (nDotV * (1.0 - k) + k);
	return gl * gv;
}

vec3 BRDF(vec3 L, vec3 V, vec3 N, float metallic, float roughness, vec3 albedo, float dist) {
	vec3 H = normalize(L + V);
	float nDotH = clamp(dot(N, H), 0.0, 1.0);
	float nDotL = clamp(dot(N, L), 0.0, 1.0);
	float nDotV = clamp(dot(N, V), 0.0, 1.0);
	float hDotV = clamp(dot(H, V), 0.0, 1.0);
	const float PI = 3.141592;

	vec3 Lo = vec3(0.0);
	if(nDotL > 0.0) {
		float D = distributionGGX(nDotH, roughness);
		vec3 F = fresnelSchlick(hDotV, albedo, vec3(0.04), metallic);
		float G = geometrySchlickGGX(nDotL, nDotV, roughness);

		vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);

		vec3 spec = D * G * F / (4.0 * nDotL * nDotV);
		float attenuation = 1.0 / (dist * dist);
		Lo += (kd * albedo / PI + spec) * attenuation * nDotL;
	}

	return Lo;
}
