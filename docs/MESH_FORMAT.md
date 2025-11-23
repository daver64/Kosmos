# Mesh Format Support

## Orbiter .MSH Format

Kosmos supports Orbiter-compatible `.msh` mesh files for 3D models.

### Format Specification

```
MSHX1                          # Header - version identifier
GROUPS <count>                 # Number of mesh groups in file
MATERIAL <index>               # Material index for this group
TEXTURE <index>                # Texture index for this group  
GEOM <vertCount> <faceCount>  ; <name>  # Geometry section start
<vertex lines>                 # Position + Normal (6 floats per line)
<face lines>                   # Triangle indices (3 ints per line)
```

### Vertex Format
Each vertex line contains 6 floats:
```
posX posY posZ normalX normalY normalZ
```

Example:
```
0.462682 -8.35536e-006 -0.490733 0.999811 0.0 -0.0194305
```

### Face Format
Each face line contains 3 vertex indices (0-based):
```
v1 v2 v3
```

Example:
```
0 21 12
```

### Multi-Group Meshes
Files can contain multiple mesh groups, each with its own material and geometry:
```
GROUPS 30
MATERIAL 1
TEXTURE 0
GEOM 1514 2472 ; Body01
<vertices>
<faces>
MATERIAL 2
TEXTURE 1
GEOM 842 1404 ; Wing_Left
<vertices>
<faces>
...
```

### Coordinate System
- Right-handed coordinate system
- Units in meters
- Origin at model center

### Loading Implementation

To load .msh files in Kosmos:

1. **Parse header** - verify `MSHX1` format
2. **Read GROUPS** count
3. **For each group:**
   - Read MATERIAL index
   - Read TEXTURE index
   - Parse GEOM header (vertex count, face count, name)
   - Read vertex data (position + normal)
   - Read face indices
   - Create OpenGL VAO/VBO for mesh group
   - Assign material and texture references

### Example Files
- `data/plugins/vessels/DeltaGlider/meshes/probe.msh` - 30 groups, 1514+ vertices per group

## DDS Texture Support

Orbiter meshes typically use **DirectDraw Surface (.dds)** textures for efficient GPU storage.

### DDS Format Features
- **Compression**: DXT1 (RGB), DXT3 (RGBA), DXT5 (RGBA with alpha), DX10
- **Mipmaps**: Built-in mipmap levels
- **Cubemaps**: Six-face environment maps
- **Volume textures**: 3D textures
- **GPU-friendly**: Direct upload to GPU without decompression

### Required Libraries
To load DDS textures, we need one of:

1. **stb_image_dds** - Header-only, simple integration
2. **gli (OpenGL Image)** - Full-featured texture library
3. **DirectXTex** - Microsoft's official library (Windows only)
4. **Manual parsing** - Implement DDS loader from spec

### DDS Files in Project
```
data/plugins/vessels/DeltaGlider/textures/
├── probe02.dds  (1024x1024, DX10 compressed)
└── probe03.dds  (128x128, DX10 compressed)
```

### Texture Naming Convention
Orbiter uses suffixes for texture types:
- `_d` or no suffix - Diffuse/color map
- `_n` - Normal map
- `_s` - Specular map
- `_e` - Emissive map
- `_lmask` - Light mask (for illuminated windows)

### Implementation Priority
1. ⏳ **MSH parser** - Load Orbiter mesh format
2. ✅ **DDS loader** - DDS texture support implemented in DDSLoader.cpp
   - Supports DXT1/3/5 compressed formats
   - Handles both compressed and uncompressed textures
   - Automatic mipmap loading
   - Tested with DeltaGlider probe textures (1024x1024 and 128x128)
3. ⏳ **Material system** - Handle multiple textures per mesh
4. ⏳ **Mesh renderer** - Render multi-group meshes with materials

## Related Documentation
- See `data/textures/TEXTURE_SOURCES.md` for texture downloading
- See `docs/STARFIELD_RENDERING.md` for rendering architecture
- See `include/Kosmos/PluginAPI.h` for vessel plugin interface
