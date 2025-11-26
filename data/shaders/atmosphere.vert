#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec3 ViewDir;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    // Transform to world space (camera-relative)
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    
    // Normal in world space
    Normal = normalize(mat3(model) * aNormal);
    
    // View direction (from fragment to camera at origin)
    ViewDir = normalize(-FragPos);
    
    gl_Position = projection * view * worldPos;
}
