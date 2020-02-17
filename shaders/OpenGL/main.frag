#version 440 core

const float PI = 3.14159265359;

uniform vec4 sunColor;
uniform float minBias;
uniform float maxBias;
uniform float farPlane;

// in vars
in vec3 PosViewspace;
in vec3 PosWorldspace;
in vec2 uv;
in vec3 normalViewspace;
in vec4 FragPosLightSpace;

in vec3 directionalLightPosition;
in vec3 directionalLightPositionViewSpace;
in vec3 pointLightPosition;
in vec3 pointLightPositionViewSpace;

in vec3 pointLightDirection;
in vec3 dirLightDirection;
in vec3 cameraDirection;

in vec3 cameraPos;

// output data back to our openGL program
out vec4 final_color;

// constant mesh values
layout(binding = 0) uniform sampler2D meshTexture;
layout(binding = 1) uniform sampler2D shadowMap;
layout(binding = 2) uniform samplerCube shadowMapOmni;
layout(binding = 3) uniform sampler2D normalMap;

struct DirectionalLight {
	vec3 position;
	vec3 color;
};

struct PointLight {
	vec3 position;
	vec3 color;
	float constant, linear, quad;
};

vec3 doLight(PointLight light);
vec3 doLight(DirectionalLight light);
float getShadow(PointLight light);
float getShadow(DirectionalLight light);

vec3 TangentNormal;

void main()
{
	TangentNormal = texture(normalMap, uv).rgb;
	TangentNormal = normalize(TangentNormal * 2.0 - 1.0);

	#ifdef NO_NORMAL_MAP
	TangentNormal = normalize(normalViewspace);
	#endif

	DirectionalLight dirLight;
	dirLight.color = sunColor.rgb;
	dirLight.position = directionalLightPositionViewSpace;

	PointLight light;
	light.position = pointLightPosition;
	light.color = vec3(1.0, 1.0, 1.0);
    light.constant = 0.0f;
    light.linear = 0.7;
    light.quad = 1.8;

	vec4 sampled = texture(meshTexture, uv);

    vec3 result = doLight(light);
	result += doLight(dirLight);

    final_color = vec4(result, sampled.a);
}

float getShadow(DirectionalLight light) {
    vec3 projCoords = FragPosLightSpace.xyz / FragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    float closestDepth = texture(shadowMap, projCoords.xy).r; 
    float currentDepth = projCoords.z;

    float bias = max(maxBias * (1.0 - dot(TangentNormal, dirLightDirection)), minBias);
    
	// simplest PCF algorithm
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;    
        }    
    }
    shadow /= 9.0;
    
    // keep the shadow at 0.0 when outside the far plane region of the light's frustum
    if(projCoords.z > 1.0) shadow = 0.0;
        
    return shadow;
}

// array of offset direction for sampling
vec3 gridSamplingDisk[20] = vec3[]
(
   vec3(1, 1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1, 1,  1), 
   vec3(1, 1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1, 1, -1),
   vec3(1, 1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1, 1,  0),
   vec3(1, 0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1, 0, -1),
   vec3(0, 1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0, 1, -1)
);

float getShadow(PointLight light) {
	vec3 frag2light = PosWorldspace - light.position;
	float currentDepth = length(frag2light);

	float closestDepth = texture(shadowMapOmni, frag2light).r;
	closestDepth *= farPlane;
	
	// DEBUG
	//final_color = vec4(vec3(closestDepth / farPlane), 1.0);  

	// PCF sampling
	float shadow = 0.0;
    float bias = 0.25;
    int samples = 20;
    float viewDistance = length(cameraPos - PosWorldspace);
    float diskRadius = (1.0 + (viewDistance / farPlane)) / farPlane;
    for(int i = 0; i < samples; ++i) {
        float closestDepth = texture(shadowMapOmni, frag2light + gridSamplingDisk[i] * diskRadius).r;
        closestDepth *= farPlane;   // undo mapping [0;1]
        if(currentDepth - bias > closestDepth)
            shadow += 1.0;
    }
    shadow /= float(samples);

	return shadow;
}


vec3 doLight(PointLight light) {
	vec4 sampled = texture(meshTexture, uv);

    // ambient
    vec3 ambient = 0.1 * sampled.xyz;

    float diff = clamp(dot(TangentNormal, pointLightDirection), 0, 1);
    vec3 diffuse = light.color * diff * sampled.rgb;

    // specular
    vec3 halfway_dir = normalize(pointLightDirection + cameraDirection);
    vec3 reflect_dir = reflect(-pointLightDirection, TangentNormal);
    float spec = pow(max(dot(halfway_dir, TangentNormal), 0.0), 32.0f);
    vec3 specular = vec3(1.0, 1.0, 1.0) * spec * sampled.rgb;

    // distance between the light and the vertex
    float distance = length(pointLightPositionViewSpace - PosViewspace);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quad * (distance * distance));

    ambient = ambient * attenuation;
    diffuse = diffuse * attenuation;
    specular = specular * attenuation;

	float shadowAmount = 1.0 - getShadow(light);
	diffuse *= shadowAmount;
	specular *= shadowAmount;

    return (ambient + diffuse + specular);
}

vec3 doLight(DirectionalLight light) {
	vec4 sampled = texture(meshTexture, uv);
    // vec4 sampled = vec4(0.0, 1.0, 0.0, 1.0);
    // ambient
    vec3 ambient = 0.1 * sampled.xyz;
	
	// diffuse
    float diff = clamp(dot(TangentNormal, dirLightDirection), 0, 1);
    vec3 diffuse = light.color * diff * sampled.rgb;

    // specular
    vec3 halfway_dir = normalize(dirLightDirection + cameraDirection);
    vec3 reflect_dir = reflect(-dirLightDirection, TangentNormal);
    float spec = pow(max(dot(halfway_dir, TangentNormal), 0.0), 32.0f);
    vec3 specular = vec3(1.0, 1.0, 1.0) * spec * sampled.rgb;

	float shadowAmount = 1.0 - getShadow(light);
	diffuse *= shadowAmount;
	specular *= shadowAmount;

    return (ambient + diffuse + specular);
}