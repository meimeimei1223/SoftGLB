#version 330 core

in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;

uniform sampler2D texture_map;
uniform bool      useTexture;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 viewPos;
uniform vec4 vertColor;

out vec4 frag_color;

void main()
{
    float ambientFactor = 0.3;
    vec3 ambient = lightColor * ambientFactor;

    vec3 normal   = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float NDotL   = max(dot(normal, lightDir), 0.0);
    vec3 diffuse  = lightColor * NDotL;

    float specularFactor = 0.8;
    float shininess      = 32.0;
    vec3 viewDir  = normalize(viewPos - FragPos);
    vec3 halfDir  = normalize(lightDir + viewDir);
    float NDotH   = max(dot(normal, halfDir), 0.0);
    vec3 specular = lightColor * specularFactor * pow(NDotH, shininess);

    vec3 lighting = ambient + diffuse + specular;

    if (useTexture) {
        vec4 texel = texture(texture_map, TexCoord);
        frag_color = vec4(lighting, 1.0) * texel;
    } else {
        frag_color = vec4(lighting, 1.0) * vertColor;
    }
}
