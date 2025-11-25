#version 410 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in vec3 Color;

out vec4 FragColor;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 objectColor;
uniform float emissive;  // 1.0 for self-luminous (stars), 0.0 for planets
uniform bool useTexture; // Whether to use texture or objectColor
uniform sampler2D textureSampler;

void main() {
    vec3 baseColor;
    
    if (useTexture) {
        baseColor = texture(textureSampler, TexCoord).rgb;
    } else {
        baseColor = objectColor;
    }
    
    if (emissive > 0.5) {
        // Self-luminous object (star) - render at full brightness
        FragColor = vec4(baseColor, 1.0);
    } else {
        // Ambient
        float ambientStrength = 0.3;
        vec3 ambient = ambientStrength * vec3(1.0);
        
        // Diffuse
        vec3 norm = normalize(Normal);
        vec3 lightDir = normalize(lightPos - FragPos);
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 diffuse = diff * vec3(1.0);
        
        // Specular
        float specularStrength = 0.5;
        vec3 viewDir = normalize(viewPos - FragPos);
        vec3 reflectDir = reflect(-lightDir, norm);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
        vec3 specular = specularStrength * spec * vec3(1.0);
        
        vec3 result = (ambient + diffuse + specular) * baseColor;
        
        FragColor = vec4(result, 1.0);
    }
}
