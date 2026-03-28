#version 330 core 

layout (location = 0) in vec3 pos;			
layout (location = 1) in vec3 normal;	
layout (location = 2) in vec2 texCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;

void main()
{
    FragPos = vec3(model * vec4(pos, 1.0f));			// vertex position in world space
	//Normal = normal;
	    // 法線をモデル変換に従って変換
    Normal = mat3(transpose(inverse(model))) * normal;
	TexCoord = texCoord;

	gl_Position = projection * view *  model * vec4(pos, 1.0f);
}

