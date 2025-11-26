#version 410 core

in vec3 FragPos;
in vec3 Normal;
in vec3 ViewDir;

out vec4 FragColor;

uniform vec3 planetCenter;     // Planet center in camera space
uniform float planetRadius;     // Planet radius
uniform float atmosphereRadius; // Atmosphere outer radius
uniform vec3 atmosphereColor;   // Base atmosphere color (e.g., blue for Earth)
uniform vec3 lightDir;          // Direction to sun (normalized)

// Atmospheric scattering parameters
const float scatterStrength = 2.5;
const float densityFalloff = 3.5;
const float fresnelPower = 3.0;
const float specularPower = 64.0;
const float specularStrength = 0.4;

void main() {
    // Calculate atmosphere effect
    vec3 normal = normalize(Normal);
    vec3 viewDir = normalize(ViewDir);
    
    // Softer Fresnel effect - atmosphere is more visible at grazing angles
    float fresnel = 1.0 - abs(dot(viewDir, normal));
    fresnel = pow(fresnel, fresnelPower);
    
    // Atmospheric density based on altitude
    float distFromCenter = length(FragPos - planetCenter);
    float altitude = (distFromCenter - planetRadius) / (atmosphereRadius - planetRadius);
    altitude = clamp(altitude, 0.0, 1.0);
    
    // Softer density falloff with altitude for more gradual fade
    float density = exp(-altitude * densityFalloff);
    
    // Additional softness layer - creates more gradual transition
    float softness = smoothstep(0.0, 0.8, 1.0 - altitude);
    density *= softness;
    
    // Diffuse lighting from sun (softer, more scattered)
    float sunDot = dot(normal, lightDir);
    float diffuse = max(sunDot, 0.0);
    
    // Subsurface scattering approximation - light wraps around
    float wrap = 0.5;
    float subsurface = max((sunDot + wrap) / (1.0 + wrap), 0.0);
    
    // Ambient component for realism
    float ambient = 0.3;
    
    // Combine lighting
    float lighting = ambient + diffuse * 0.5 + subsurface * 0.3;
    
    // Specular highlight (sun glint on atmosphere)
    vec3 halfDir = normalize(lightDir + viewDir);
    float specDot = max(dot(normal, halfDir), 0.0);
    float specular = pow(specDot, specularPower) * specularStrength;
    
    // Combine effects with softer blending
    float atmosphereStrength = fresnel * density * scatterStrength;
    
    // Add blue shift for backlit atmosphere (more realistic scattering)
    float backlit = max(-sunDot, 0.0);
    vec3 scatteredColor = mix(atmosphereColor, atmosphereColor * 1.3, backlit * 0.3);
    
    // Final atmosphere color with lighting and specular
    vec3 finalColor = scatteredColor * lighting + vec3(1.0) * specular;
    
    // Softer alpha falloff
    float alpha = atmosphereStrength * 0.6; // Reduced overall intensity for subtlety
    
    // Output with transparency
    FragColor = vec4(finalColor, alpha);
}
