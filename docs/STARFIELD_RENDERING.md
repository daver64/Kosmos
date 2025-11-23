# Skybox and Star Field Rendering

## Overview

Kosmos will use a multi-layer approach for background rendering:

1. **Milky Way Skybox** - Static panoramic texture (far background)
2. **Distant Stars** - Billboard sprites for stars beyond ~1000 ly
3. **Nearby Stars** - Actual 3D positioned stars within ~100 ly (rendered as small spheres with corona)

## Architecture

### 1. Skybox Renderer

Located in: `src/Rendering/StarField.cpp` (to be enhanced)

**Purpose**: Render a static background representing the Milky Way galaxy

**Technique**: Cube-mapped or equirectangular sphere mapping

```cpp
// src/Rendering/StarField.h
class StarField {
public:
    bool Initialize();
    void Render(Camera* camera);
    
    // Load skybox from equirectangular panorama
    bool LoadSkybox(const std::string& texturePath);
    
    // Or load as 6-face cubemap
    bool LoadCubemap(const std::vector<std::string>& faces);
    
private:
    std::unique_ptr<Mesh> m_skyboxMesh;     // Large sphere or cube
    std::unique_ptr<Shader> m_skyboxShader;
    std::unique_ptr<Texture> m_skyboxTexture;
    float m_brightness;
};
```

**Shader Requirements:**
- No lighting calculations
- No depth write (render first or last with depth test)
- Camera-centered (subtract camera position from view matrix)

### 2. Distant Star Field

**Purpose**: Render stars beyond ~1000 light-years as point sprites/billboards

**Data Source**: Hipparcos catalog (magnitude < 6.5 for naked-eye stars)

**Technique**: 
- GPU instanced rendering of billboard quads
- Color based on B-V color index or temperature
- Size based on apparent magnitude
- Twinkling effect (optional, with time-based noise)

```cpp
// src/Rendering/StarField.cpp enhancement
struct DistantStar {
    glm::vec3 direction;    // Unit vector from Sol
    float magnitude;        // Apparent magnitude
    float colorIndex;       // B-V color (-0.5 to 2.0)
    float size;            // Screen size multiplier
};

class StarField {
    // ... existing skybox code ...
    
    void LoadDistantStars(const std::string& catalogPath);
    void RenderDistantStars(Camera* camera);
    
private:
    std::vector<DistantStar> m_distantStars;
    GLuint m_starVBO;
    GLuint m_starVAO;
};
```

### 3. Nearby Stars (3D Objects)

**Purpose**: Render nearby stars (<100 ly) as actual 3D objects with position, allowing navigation

**Data Source**: Gliese catalog, Hipparcos nearby subset

**Technique**:
- Rendered as small spheres with glow/corona
- Full 3D positioning in galactic coordinates
- Different colors/textures per spectral type

```cpp
// src/Rendering/StarRenderer.cpp (new)
class StarRenderer {
public:
    void Initialize();
    void Render(const std::vector<Star*>& stars, Camera* camera);
    
private:
    std::map<std::string, Texture*> m_spectralTextures;  // O, B, A, F, G, K, M
    Mesh* m_sphereMesh;
    Shader* m_starShader;
    
    glm::vec3 GetStarColor(char spectralType);
    float GetStarRadius(float luminosity);
};
```

## Implementation Priority

### Phase 1: Static Skybox (Immediate)
- Load equirectangular Milky Way texture
- Render as large inverted sphere around camera
- Basic shader with no lighting
- **Result**: Beautiful background immediately visible

### Phase 2: Distant Star Billboards (Medium Priority)
- Load Hipparcos bright stars (mag < 6.5, ~5000 stars)
- Convert RA/Dec to unit vectors
- Render as point sprites with color
- **Result**: Realistic star field with proper constellations

### Phase 3: Nearby 3D Stars (Low Priority)
- Load Gliese catalog (~3800 stars within 25 parsecs)
- Convert to 3D Cartesian coordinates
- Render with corona textures
- **Result**: Navigable stellar neighborhood

## Coordinate Systems

### Skybox (Static)
- Aligned with J2000.0 equatorial coordinates
- No translation, only rotation with camera
- Always rendered at "infinite" distance

### Distant Stars (Billboards)
- Convert catalog RA/Dec to unit direction vectors
- Scale by brightness for visual importance
- No parallax (too far)

### Nearby Stars (3D)
- Convert RA/Dec/Parallax to Cartesian (x,y,z) in parsecs
- Center on Sol (0,0,0)
- Full 3D positioning
- Can navigate between them

## Texture Requirements

### Skybox Texture
- **Format**: Equirectangular (2:1 aspect ratio) or cubemap (6 faces)
- **Resolution**: 4K-8K for good quality
- **Content**: Milky Way band, nebulae, background stars
- **Source**: ESA Gaia, Celestia, or custom renders

Example:
```
data/textures/skybox/
├── milky_way_gaia_8k.png          # Equirectangular
├── milky_way_celestia_4k.jpg      # Alternative
└── cubemap/                        # Optional cubemap version
    ├── positive_x.png
    ├── negative_x.png
    ├── positive_y.png
    ├── negative_y.png
    ├── positive_z.png
    └── negative_z.png
```

### Star Sprite Texture
- Small circular gradient (white center → transparent edge)
- Optional: Color variations for different spectral types
- Size: 64x64 to 256x256 pixels

### Star Corona Textures (for nearby stars)
- Radial glow with spectral-dependent color
- Separate textures for O, B, A, F, G, K, M stars
- Size: 512x512 to 2048x2048 pixels

## Shader Examples

### Skybox Vertex Shader
```glsl
#version 410 core
layout(location = 0) in vec3 position;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main() {
    // Remove translation from view matrix
    mat4 viewNoTranslation = mat4(mat3(view));
    vec4 pos = projection * viewNoTranslation * vec4(position, 1.0);
    gl_Position = pos.xyww;  // Ensure skybox is at far plane
    TexCoords = position;
}
```

### Skybox Fragment Shader
```glsl
#version 410 core
in vec3 TexCoords;
out vec4 FragColor;

uniform samplerCube skybox;
uniform float brightness;

void main() {
    vec3 color = texture(skybox, TexCoords).rgb;
    FragColor = vec4(color * brightness, 1.0);
}
```

### Star Billboard Shader
```glsl
#version 410 core
layout(location = 0) in vec3 direction;   // Unit vector
layout(location = 1) in float magnitude;
layout(location = 2) in float colorIndex;

// Geometry shader creates billboard quad from point
// Fragment shader applies circular gradient + color
```

## Performance Considerations

### Skybox
- Render first (clear depth) or last (depth = 1.0)
- Single draw call
- Minimal performance impact

### Distant Stars
- Use instanced rendering for thousands of stars
- Frustum culling to skip stars outside view
- LOD: Fade out faint stars when camera moves fast

### Nearby Stars
- Only render stars within ~100 ly initially
- Add more as camera moves
- Spatial partitioning (octree) for efficient queries

## Data Pipeline

1. **Catalog Download** → Raw catalog files (Hipparcos, Gliese, etc.)
2. **Preprocessing** → Convert to efficient binary format
3. **Runtime Loading** → Load into GPU buffers
4. **Rendering** → Draw with appropriate technique

## Testing

Start with a simple skybox texture to verify the rendering works, then progressively add:
1. Static Milky Way background ✓
2. 100 brightest stars as points
3. All naked-eye stars (~5000)
4. Nearby stars as 3D objects

## References

- **Skybox Tutorial**: https://learnopengl.com/Advanced-OpenGL/Cubemaps
- **Star Rendering**: "Real-Time Rendering" Chapter 13
- **Hipparcos Catalog**: https://cdsarc.cds.unistra.fr/
- **Gaia Archive**: https://gea.esac.esa.int/archive/

## Next Steps

1. Implement basic `StarField::LoadSkybox()` and `StarField::Render()`
2. Download Gaia Milky Way texture
3. Create skybox shaders
4. Test with camera rotation
5. Add distant star billboards (Phase 2)
