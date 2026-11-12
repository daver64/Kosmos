Energia-M / Kosmos PBR texture replacement - first pass

Original OBJ files are copied unchanged, including ALL vertex/UV/normal ordering.
Existing UV atlas regions identified from OBJ face indices:
  Booster upper-left, core lower-left, fairing right.
Replaces missing original textures/EnerM.png with new 2048x2048 atlas.
Includes a tangent-space normal map and Pm/Pr settings in the MTLs.

IMPORTANT: original EnerM.png was NOT provided. This is a new plausible
procedural design, not a faithful recolouring of the original artwork.
The orange-brown core is a stylistic interpretation of insulated launch
vehicle surfaces, not a claim of exact Energia-M flight colours.

Keep directory layout, OBJ and MTL together with textures/ subfolder.
No OBJ index changes were made. Not yet tested in Kosmos.
