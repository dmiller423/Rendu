#version 400
in INTERFACE {
	vec3 pos;
	vec2 uv;
} In ;

uniform vec3 lightDirection;
uniform bool debugCol;
uniform vec3 camPos;

layout(binding=0) uniform sampler2D heightMap;
layout(binding=1) uniform sampler2D shadowMap;
layout(binding=2) uniform sampler2D surfaceNoise;
layout(binding=3) uniform sampler2D glitterNoise;
layout(binding=4) uniform sampler2D sandMapSteep;
layout(binding=5) uniform sampler2D sandMapFlat;

layout (location = 0) out vec3 fragColor;
layout (location = 1) out vec3 fragWorldPos;

const vec3 sunColor = vec3(1.474, 1.8504, 1.91198);

vec3 mixNormals(vec3 n1, vec3 n2, float alpha){
	vec2 base = mix(n1.xy/n1.z, n2.xy/n2.z, alpha);
	return normalize(vec3(base, 1.0));
}

void main(){

	fragWorldPos = In.pos;

	// Colors.
	vec3 sandColor = vec3(1.0, 0.516, 0.188);
	vec3 shadowColor = 0.2 * sandColor;
	vec3 specColor = vec3(1.0, 0.642, 0.378);

	// Get clean normal and height.
	vec4 heightAndNor = textureLod(heightMap, In.uv, 0.0);
	vec3 n = normalize(heightAndNor.yzw);
	vec3 v = normalize(camPos - fragWorldPos);

	// Transition weight between planar and steep regions, for both X and Z orientations.
	float wFlat = pow(abs(n.y), 50.0);
	float wXdir = abs(n.y) > 0.99 ? 0.0 : (abs(n.x)/sqrt(1.0-n.y*n.y));

	// Sand normal map, blended.
	vec2 mapsUv = 130.0 * In.uv;
	vec3 nSteepX = normalize(texture(sandMapSteep, mapsUv).rgb * 2.0 - 1.0);
	vec3 nSteepZ = normalize(texture(sandMapSteep, mapsUv.yx).rgb * 2.0 - 1.0);
	vec3 nFlat  = normalize(texture(sandMapFlat, mapsUv).rgb * 2.0 - 1.0);
	vec3 nSteep = normalize(mix(nSteepZ, nSteepX, wXdir));
	vec3 nMap = normalize(mix(nSteep, nFlat, wFlat));

	// Add extra perturbation.
	vec3 surfN = normalize(texture(surfaceNoise, 200.0*In.uv).rgb);
	vec3 baseN = normalize(mix(nMap, surfN, 0.5));

	// Build an arbitrary normal frame and map local normal.
	vec3 tn = abs(n.y) < 0.01 ? vec3(0.0,1.0,0.0) : vec3(1.0, 0.0, 0.0);
	vec3 bn = normalize(cross(n, tn));
	tn = normalize(cross(bn, n));
	mat3 tbn = mat3(tn, bn, n);
	vec3 finalN = normalize(tbn * baseN);

	// Shadow
	float shadow = textureLod(shadowMap, In.uv, 0.0).r;

	// Tweaked diffuse.
	float diffuse = clamp(4.0 * dot(vec3(1.0,0.3,1.0) * finalN, lightDirection), 0.0, 1.0);
	vec3 color = mix(shadowColor, sandColor, shadow * diffuse);

	// Fresnel.
	float F = pow(1.0 - max(dot(finalN, v), 0.0), 5.0);
	// Specular lobe.
	vec3 h = normalize(v + lightDirection);
	float lobe = pow(max(dot(finalN, h), 0.0), 256.0);
	// Extra glitter
	vec2 glitterUV = 50.0 * In.uv + vec2(0.71, 0.23);
	vec3 glitterN2 = normalize(texture(glitterNoise, glitterUV).rgb);
	vec3 lightBounce = reflect(-lightDirection, glitterN2);
	float glit = smoothstep(0.9, 1.0, dot(lightBounce, v));
	// Total specular.
	vec3 spec = (shadow * 0.9 + 0.1) * (F * specColor + lobe * specColor + 2.0 * glit * specColor);
	
	color += spec;
	//color *= sunColor;
	
	if(debugCol){
		color = vec3(0.9,0.9,0.9);
	}
	fragColor = color;
}
