#version 330 core

in vec3 TexCoords;
out vec4 FragColor;

uniform sampler2D skyboxTexture;

const float PI = 3.14159265359;

void main()
{
    // Convert cube coordinates to equirectangular (longitude/latitude)
    vec3 dir = normalize(TexCoords);
    
    // Calculate longitude (u) and latitude (v) from direction
    float u = 0.5 + atan(dir.z, dir.x) / (2.0 * PI);
    float v = 0.5 - asin(dir.y) / PI;
    
    // Sample the equirectangular texture
    vec3 color = texture(skyboxTexture, vec2(u, v)).rgb;
    
    FragColor = vec4(color, 1.0);
}
