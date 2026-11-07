# Soyuz model source and notices

The models in this add-on were converted from the Orbiter CVE-lite Soyuz
Launchers and SoyuzTMA add-ons. The original meshes used by the converted
models are included here. The Kosmos OBJ and MTL files are in `../models/`.
The original Orbiter coordinates were converted to Kosmos coordinates;
the spacecraft mesh was also split into components for staging.

## Soyuz launcher

The upstream `SoyuzLaunchers.txt` credits Urwumpe for the main meshes and
says his and all the meshes except OrbiterFan's Mars Express meshes are
released under the GNU LGPL. The included `launcher/` meshes correspond
by name to the launcher OBJ files; no Mars Express meshes are included.
The converted launcher materials do not refer to external textures.

## Soyuz TMA and parachute

The upstream `SoyuzTMA.txt` and `SoyuzTMA_mesh_copyright.txt` credit Jason
Benson for the Soyuz meshes and textures and state that they were placed
in the public domain. Benson's quoted permission says the meshes may be
used and modified as long as he is credited. `SoyuzTMA.txt` thanks Jógvan
Trondesen for adapting the meshes for animation and also credits him in
its copyright section.

The spacecraft models `soyuz.obj`, `tma-descent.obj`, `tma-orbital.obj`,
and `tma-service.obj` are derived from `tma/soyuz.msh`;
`soyuz-descent.obj` is derived from `tma/soyuz-descent.msh`. Their seven
`SOY-*.png` textures were decoded from the corresponding source DDS
textures.

The same upstream `SoyuzTMA.txt` credits D Henderson for the parachute
meshes and states that they are released under the GNU LGPL. `chute.obj`
is derived from `tma/soychute.msh`; its materials do not refer to external
textures.

The upstream notices say GNU LGPL without specifying a version. A copy of
the [GNU Lesser General Public License, version 2.1](LICENSE-LGPL-2.1.txt)
is provided here; its inclusion does not claim that the upstream notice
specified that version. The original meshes remain available in the
directories described above.
