Titan III/IV PBR materials

The MTL files use simlib's scalar Pm metallic and Pr roughness values, with
tangent-space maps from map_Bump. The normal maps are generated from the
existing Titan texture atlases by tools/gen_titan_normals.py. They add subdued
surface relief; the original colour textures, UVs and meshes are unchanged.
