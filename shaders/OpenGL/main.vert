#version 440 core

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_binormal;

uniform mat4 model, view, projection;
uniform mat4 lightSpaceMatrix;
uniform vec4 cameraPosition;
uniform vec4 DirLightPos;
uniform vec4 pointLightPos;

//we send out a uv coordinate for our frag shader
out vec3 PosViewspace;
out vec3 PosWorldspace;
out vec2 uv;
out vec3 normalViewspace;
out vec4 FragPosLightSpace;

out vec3 directionalLightPosition;
out vec3 directionalLightPositionViewSpace;
out vec3 pointLightPosition;
out vec3 pointLightPositionViewSpace;

out vec3 pointLightDirection;
out vec3 dirLightDirection;
out vec3 cameraDirection;
out vec3 cameraPos;

void main()
{
	// stuff in view space
	PosViewspace = vec3(view * model * vec4(v_pos, 1.0));
	directionalLightPositionViewSpace = vec3(view * DirLightPos);

	PosWorldspace = vec3(model * vec4(v_pos, 1.0));
	normalViewspace = mat3(transpose(inverse(view * model))) * v_normal;
    uv = v_uv;
	FragPosLightSpace = lightSpaceMatrix * vec4(PosWorldspace, 1.0);
    gl_Position = projection * view * model * vec4(v_pos, 1.0);

    //////////////////////////
	pointLightPosition = vec3(pointLightPos);
	pointLightPositionViewSpace = vec3(view * pointLightPos);
	directionalLightPosition = vec3(DirLightPos);
	cameraPos = vec3(cameraPosition);

	// calculate tangent space stuff
	vec3 T = normalize(vec3(view * model * vec4(v_tangent, 0.0)));
	vec3 B = normalize(vec3(view * model * vec4(v_binormal, 0.0)));
	vec3 N = normalize(vec3(view * model * vec4(v_normal, 0.0)));
	mat3 TBN = transpose(mat3(T, B, N));

	#ifdef NO_NORMAL_MAP
	TBN = mat3(1.0f);
	#endif

	// calculate the light directions for tangent space
	pointLightDirection = TBN * normalize(vec3(view * pointLightPos) - PosViewspace);
	dirLightDirection = TBN * normalize(vec3(view * DirLightPos) - PosViewspace);
	cameraDirection = TBN * normalize(-PosViewspace);
}