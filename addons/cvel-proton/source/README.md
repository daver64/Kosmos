# CVEL Proton-K source and attribution

The original `Proton-k.txt` credits D Henderson, Urwumpe for the second
stage, and Alex v Hohlov for the first and third stages. It releases all
meshes under the Artistic Licence; the licence text and the five original
launcher meshes are included here. The accompanying DDS textures are
from the CVEL Proton distribution; the notice does not specify their terms
separately.

Kosmos converted the meshes to OBJ/MTL, reflected the Z axis, converted the
referenced DDS textures to PNG, and removed coincident faces. The conversion
is reproducible with `tools/import_cvel_proton.py` and `tools/msh_import.py`.
The source distribution lacks the second-stage `ProtonStage2.dds`; until it
is available, that stage uses its original white material colour.
The original notice's LGPL statement applies to the source code and modules,
not the meshes.
