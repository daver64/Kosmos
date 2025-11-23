#version 330 core

layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    // Remove translation from view matrix (keep rotation only)
    mat4 viewNoTranslation = mat4(mat3(view));
    
    vec4 pos = projection * viewNoTranslation * vec4(aPos, 1.0);
    
    // Set z = w to ensure skybox is always at maximum depth
    gl_Position = pos.xyww;
    
    // Use position as texture coordinates for cube
    TexCoords = aPos;
}
