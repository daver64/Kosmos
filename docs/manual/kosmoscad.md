# KosmosCAD manual

KosmosCAD is a 3D modeller for Kosmos ships (Linux). It shows your model in four views at once, lets you pick and edit meshes down to single vertices, and places engines, gear, tanks, ISRU units, reactors and pressure vessels as markers you can drag. It is simpler than Blender, closer to Anim8or. Every view is drawn with OpenGL 3.2.

![KosmosCAD with a cylinder: Top \(top left\), Front \(top right\), Side \(bottom left\) and Perspective \(bottom right, with the orange frame because it is the active view\).](img/cad/cylinder-views.png)KosmosCAD with a cylinder: Top (top left), Front (top right), Side (bottom left) and Perspective (bottom right, with the orange frame because it is the active view).

<a id="start"></a>

## Starting
    
    
    addons/kosmoscad/bin/kosmoscad                       # empty scene
    addons/kosmoscad/bin/kosmoscad addons/MYSHIP/ship.json  # a ship
    addons/kosmoscad/bin/kosmoscad scene.kmodel          # a scene file
    addons/kosmoscad/bin/kosmoscad ship.json --shot out.png --size 1280x800   # picture, then exit

It also starts from the **Model in KosmosCAD** button in ShipEdit (save the ship as an addon first), or build it with `cmake --build build --target kosmoscad`. It needs no other program.

**File > Open** only accepts a `ship.json` (a file with a `"stages"` list). Another JSON file gives a warning and nothing opens. A ship with no OBJ model opens too, with a warning; only its engines, gear and systems appear. **File > Close file** (Ctrl+W) clears the editor, asking first if there are unsaved changes.

<a id="window"></a>

## The window

  * **Views:** Top (top left), Front (top right), Side (bottom left), Perspective (bottom right). The active view (last one you clicked) has an orange frame. Space maximises it and restores it.
  * **Views** fill the whole window. The panels are floating tool windows you can drag anywhere, even onto a second screen, and show or hide with the **Scene** and **Properties** toolbar buttons, the View menu, or `F2` / `F3`.
  * KosmosCAD remembers where you put the windows (and which were open) in `~/.config/kosmoscad/layout.txt`, and the status bar reminds you of the keys when it starts.
  * **Modify menu** : extrude, inset, delete and flip faces, split edge, weld, mirror, union / subtract / intersect, apply transform, origin to centre, extrude and lathe sketch. The distances come from the Modelling section of the Properties window, which has the same buttons.
  * **Scene window** (`F2`): objects and markers. Click to select, Shift+click to add or remove.
  * **Properties window** (`F3`): Transform (position, rotation, scale of the selection), Equipment properties, Modelling buttons, Materials and UVs.
  * **Toolbar:** mode buttons Object / Vertex / Edge / Face (keys 1 to 4) and tool buttons Select / Move / Rotate / Scale (Q, W, E, R).

![The empty window: the menu bar, the mode and tool buttons, and the four views.](img/cad/start.png)The empty window: the menu bar, the mode and tool buttons, and the four views.

The axes follow the Kosmos ship frame: X right, Y up, the nose at -Z. In the Top view the nose points up the screen; in the Side view it points right; in the Front view +X points to the left, because you look at the ship from the nose.

<a id="nav"></a>

## Navigating the views

  * **Orbit** (perspective view): middle or right drag, or Alt+left drag.
  * **Pan:** the same buttons in the Top, Front and Side views.
  * **Zoom:** the mouse wheel.
  * **F** frames everything, **Z** cycles the active view through wireframe, solid and textured, **U** opens the UV editor, **G** the grid. **View > Mesh edges** shows or hides the edge overlay.



<a id="select"></a>

## Selecting and transforming

  * **Object mode:** click an object or marker; drag an empty area for a box selection; Shift+click toggles an object in or out of the selection. The first object you pick is the active one.
  * **Vertex, Edge, Face mode:** act on the active object. Vertices show as dots, selected edges are highlighted, selected faces are tinted.
  * **Move, Rotate, Scale** show a gizmo in every view. Drag a coloured arrow to move along an axis; drag the centre square to move in the view plane. Rotate and Scale use the same handles: drag along the handle (right/up increases). Each drag is one undo step.
  * You can also type exact values in the Transform section.
  * **Ctrl+Z** undoes, **Ctrl+Y** or Ctrl+Shift+Z redoes, **Ctrl+D** duplicates, **Del** or **X** deletes, **Ctrl+A** selects all, **Esc** clears the selection.



<a id="shapes"></a>

## Creating shapes

**Add** menu: Box, Cylinder, Cone / frustum, Sphere, Torus, Plane. Each asks for its size and segment counts, then appears at the origin.

**Sketch:** choose Add > Sketch (or the Sketch button) with the Top, Front or Side view active. Click points to draw an outline (right-click removes the last point, Backspace too). Then:

  * **Enter** (Extrude sketch): asks for a depth and a far-end scale. A scale other than 1 makes a loft, a taper from the outline to a scaled copy.
  * **L** (Lathe): spins the outline around the vertical axis; only from the Front or Side view, with the distance from the axis as the radius.
  * **Esc** cancels.

![The Add menu: the basic shapes, Sketch, and Equipment markers.](img/cad/add-menu.png)The Add menu: the basic shapes, Sketch, and Equipment markers.

<a id="edit"></a>

## Editing meshes

Modelling section of the Properties window:

  * **Extrude** (Face mode): pushes the selected faces out by the distance beside the button. **Inset** shrinks them into a smaller face to build up details.
  * **Delete faces** , **Flip normals** (a face looking the wrong way draws dark or backwards), **Split edge** (a vertex in the middle of each selected edge), **Weld** (merge vertices closer than the given distance).
  * **Mirror X / Y / Z** : adds a mirrored copy of the mesh to the same object, reflected through the object's own origin plane. It is the quick way to a symmetric ship: build the half away from the centre line and do not move the object afterwards.
  * **Union, Subtract, Intersect** : select two or more objects; the active one keeps the result. Subtract cuts the others out of it, for example a bay or a window. Booleans work best on closed, clean meshes.
  * **Apply rotation+scale** bakes them into the vertices; **Origin to centre** moves the pivot to the middle of the object.



<a id="materials"></a>

## Materials and UVs

  * Each face has a material. **New material** creates one (name, colour, optional texture image path); **Assign to selection** puts it on the selected faces (Face mode) or on all faces of the selected objects.
  * **Painting a material.** Each material has a _Colour_ , a _Texture_ (the colour image), a _Normal_ map (bump detail), a _Spec map_ (a metallic-roughness image: green = roughness, blue = metallic), _Metallic_ and _Roughness_ sliders and an _Emissive_ colour for lit windows and panels. The **...** buttons browse for an image (PNG, JPG, DDS). The Textured view shows the normal map, highlights and emissive light as you change them. In an OBJ part the colour texture, normal map (`map_Bump`), metallic (`Pm`), roughness (`Pr`) and emissive (`Ke`) are all used by Kosmos. The spec map is only written to GLB (it uses glTF's layout, so it is not saved as `map_Ks`).
  * **The object name is the part name Kosmos can animate** : KosmosCAD writes each object as an `o` line in the OBJ, so a door that opens is just an object called `door` that the ship's animation JSON names. Materials only describe the look, and one object may use several. (Older files without `o` lines still use material names, and animations may name either.)
  * **Textures stay with the model.** Image paths are stored relative to the model's folder (the `.kmodel`'s, or the `ship.json`'s), and the images should live in that folder or one below it. Pick an image from anywhere else and KosmosCAD copies it into a `textures` folder beside the model and stores `textures/name.png`; a name that is already taken gets a number. A scene that has not been saved yet keeps the full path until its first save, when the images are copied in the same way. Older files with absolute paths are fixed the next time you save them. This keeps a model folder movable and shareable.
  * **UV Project** (Planar, Box, Cylindrical, Spherical on the X, Y or Z axis) generates texture coordinates for the selection. The path is saved with the material and copied beside the OBJ on export.
  * **Textured view.** Press **Z** (or Wire/Solid/Textured in the toolbar) until the active view says _textured_ : every material shows its texture image, lit, with PNG, JPG and Orbiter DDS files all supported. Relative texture paths are looked up beside the `ship.json` or `.kmodel`. A material with no image (or one that cannot be read) shows its plain colour. The checker is handy for seeing how stretched a mapping is: tick **Checker instead of the image** in the UV editor panel to use it everywhere.
  * **Render image.** View > Render... opens a window showing the perspective view, textured, with a Save as PNG button; it is drawn without the grid, gizmos or selection tint, at the size of the view on screen.



<a id="uv-editor"></a>

## UV editor

![The UV editor \(left\) beside the textured Perspective view. The selected face is outlined in the 0..1 square.](img/cad/uv-editor.png)The UV editor (left) beside the textured Perspective view. The selected face is outlined in the 0..1 square.

Press **U** (or the UV editor button) and the window becomes a Blender-style pair: the UV editor on the left, the textured perspective view on the right. The editor shows the material's image under the 0..1 square, with the UV wireframe of what you have selected (the selected faces in Face mode, otherwise all the faces of the selected objects) drawn over it. In Face mode the other faces of the object are drawn faintly for context. The UV editor panel has tick boxes to hide the UV points (selected points always show) and to turn the faint faces off. Faces with no UVs are box-projected for you when they come into the editor, and faces that shared UVs with others get their own, so changing one never disturbs another.

  * **Slide the image around:** drag with the left button. The tool decides what a drag does: **Move (G)** slides the mapping left, right, up and down, **Rotate (R)** turns it about its centre, **Scale (S)** grows or shrinks it about its centre. The part updates live in the perspective view, and one drag is one undo step.
  * **Only some points:** click a UV point to select it (Shift adds or removes), or Ctrl+drag a box. With nothing selected the tools act on every UV in the editor.
  * **Pan and zoom:** middle or right drag (or Alt+left drag) pans, the wheel zooms about the cursor, **F** frames the 0..1 square.
  * **Buttons:** Flip U, Flip V, Rotate 90, Fit to 0..1, and the Offset / Rotate / Scale numbers with **Apply numbers** for exact values (scaled and rotated about the centre, then offset).
  * Choosing another material in the Materials panel shows that material's image under the same UVs, so you can line a face up with a different texture. **Assign to selection** then gives the faces that material.
  * Press **U** again to return to the four views.



<a id="equipment"></a>

## Equipment markers

Add > Equipment places engines, landing gear, docking ports, vessels (tanks), wheels and ship systems (reactor, ISRU, life support, pressure vessel and so on). A marker is a small shape with an arrow for its direction (an engine's thrust, a port's approach). Drag it with the gizmo like any object. Its numbers (thrust, Isp, capacity and so on) appear in **Equipment properties** as JSON; edit them and press **Apply properties**. When you save to `ship.json`, markers go back into the right section (`engines`, `gear`, `ports`, `vessels`, `wheels`, `systems`); keys the editor does not know are kept untouched.

<a id="files"></a>

## Files

  * **Save scene (.kmodel)** , Ctrl+S: KosmosCAD's own file with everything, including undo-independent materials and markers.
  * **Save to ship.json** , Ctrl+Shift+S: writes the meshes as OBJ+MTL in the ship's `models/` folder and updates `ship.json`, keeping the previous one as `ship.json.bak`. Restart Kosmos or reload the ship to see it.
  * **Import mesh** reads OBJ, Orbiter `.msh` and glTF/GLB (File > Open does the same for a single mesh); **Export** writes OBJ or GLB.
  * **Render...** (View menu) opens a window with the rendered image; use its Save as PNG, Copy or Render again buttons.
  * **Editing parts from the sim.** In the part editor (and the base editor) pick _Mesh editor: KosmosCAD_ instead of Blender. The sim pauses and starts `kosmoscad --edit-mesh FILE`; Ctrl+S (File > Send to Kosmos) rewrites the .glb or .obj and the sim reloads it, and closing KosmosCAD resumes. Blender remains supported and has far more features; KosmosCAD is the simple editor that knows engines, habitats and systems. It cannot write .gltf parts.

![The File menu: Open, Import mesh, Send to Kosmos, the two save formats and Export OBJ / GLB.](img/cad/file-menu.png)The File menu: Open, Import mesh, Send to Kosmos, the two save formats and Export OBJ / GLB.

<a id="keys"></a>

## Keys

Key| Action  
---|---  
1 2 3 4| Object, Vertex, Edge, Face mode  
Q W E R| Select, Move, Rotate, Scale  
F / Space / Z / G| Frame all / maximise view / wire-solid-textured / grid  
U| UV editor on or off (then G / R / S pick Move / Rotate / Scale)  
Del or X| Delete selection  
Ctrl+N, O, W| New, Open, Close file  
Ctrl+S, Ctrl+Shift+S| Save scene, save ship.json  
Ctrl+Z, Ctrl+Y, Ctrl+D, Ctrl+A| Undo, redo, duplicate, select all  
Enter / L / Esc / Backspace| Sketch: extrude / lathe / cancel / remove last point  
  
<a id="tutorial-cylinder"></a>

## Tutorial: a low-poly cylinder with a colour texture

A short first lesson: make a drum with eight sides, paint it with an image and save it. You need any PNG or JPG to use as the texture (a stripe, a logo, a photo; a square image of about 256 to 1024 pixels is fine). The pictures below use this one, which you can [download](img/cad/drum-texture.png) to follow along.

![The texture used in this tutorial \(512 x 512\).](img/cad/drum-texture.png)The texture used in this tutorial (512 x 512).

  1. **Start.** Run `kosmoscad` with no file. Press **1** for Object mode.
![Step 1: the empty window, Object mode.](img/cad/start.png)Step 1: the empty window, Object mode.
  2. **The cylinder.** Add > Cylinder. Set Radius 1, Height 2 and **Segments 8** , then OK. Fewer segments means fewer sides, which is what makes it low poly: 8 gives an octagonal drum. The cylinder stands upright on the Y axis with a cap at each end.
![Step 2: Add > Cylinder asks for Radius, Height and Segments. Segments 8 gives the low-poly octagonal drum.](img/cad/cylinder-dialog.png)Step 2: Add > Cylinder asks for Radius, Height and Segments. Segments 8 gives the low-poly octagonal drum.
  3. **Look at it.** Click the Perspective view and press **F** to frame it. Drag with the middle mouse button to orbit. Press **Z** with the view active to cycle wireframe, solid and textured; you can see the eight sides and the two caps. Add > Cylinder again with other segment counts if you want to compare.

![Wireframe: eight sides, two caps.](img/cad/persp-wire.png)Wireframe: eight sides, two caps. ![Solid: lit flat colour, one grey-tan material.](img/cad/persp-solid.png)Solid: lit flat colour, one grey-tan material.

  4. **Make a material.** Press **F3** for the Properties window and find _Materials and UVs_. Press **New material** , the new material is called `part_2` or similar: select the whole Name text (click it, then Ctrl+A), type `drum` and press Enter. Click the colour swatch to pick a base colour (the colour is used wherever there is no texture, and in the plain solid view).
![Step 4: the Materials and UVs section \(scroll down in the Properties window\) with the new material <code>drum</code>.](img/cad/material-named.png)Step 4: the Materials and UVs section (scroll down in the Properties window) with the new material `drum`.
  5. **Load the texture.** Press the **...** button beside _Texture_ and choose your image. Press **Assign to selection** so the drum uses the new material (with the object selected, every face of it is assigned).
![Step 5: the image chooser. Pick the PNG, JPG or DDS file; Ctrl+L lets you type a path.](img/cad/file-chooser.png)Step 5: the image chooser. Pick the PNG, JPG or DDS file; Ctrl+L lets you type a path.
  6. **Give it UVs.** UVs say which part of the image lands on which face. In the UV row choose **Cylindrical** , axis **Y** , and press **Project**. The image now wraps once around the sides. (Box projection also works, and is the better choice if you want the caps painted too.)
![Step 6: the texture path is set, the UV row reads Cylindrical, Y, and Project has been pressed.](img/cad/uv-project.png)Step 6: the texture path is set, the UV row reads Cylindrical, Y, and Project has been pressed.
  7. **See it.** Press **Z** until the Perspective view says _textured_. The drum shows your image.
![Step 7: the textured Perspective view. The image wraps once around the eight sides; the caps show only the top and bottom edge of the image.](img/cad/persp-textured.png)Step 7: the textured Perspective view. The image wraps once around the eight sides; the caps show only the top and bottom edge of the image.
  8. **Adjust the mapping.** Press **U**. The UV editor opens beside the 3D view, with your image and the drum's UV wireframe on top. Select a face in the 3D view (press **4** for Face mode and click it) to see its UVs. Drag to move them, press **R** to rotate and **S** to scale; the 3D view updates as you drag. **Fit to 0..1** stretches the selection over the whole image. Press **U** again to close the editor.
![Step 8: the UV editor with one side face selected in Face mode. Its UV quad is the outlined strip.](img/cad/uv-editor.png)Step 8: the UV editor with one side face selected in Face mode. Its UV quad is the outlined strip.
  9. **Optional extras.** Add a _Normal_ map for bump detail, and set _Roughness_ and _Metallic_ for a shinier or more matt surface. Press **Z** to confirm how it looks.
  10. **Save and render.** File > Save scene (`Ctrl+S`) writes a `.kmodel`. View > Render... shows a picture of the drum, with a Save as PNG button. To use it in a ship, File > Export OBJ... (or Export GLB...) writes the mesh; the texture is copied into a `textures` folder beside it.
![Step 10: the Render window, with Save as PNG, Copy to clipboard and Render again.](img/cad/render.png)Step 10: the Render window, with Save as PNG, Copy to clipboard and Render again. ![File > Export OBJ... or Export GLB... writes the mesh for use in Kosmos.](img/cad/file-menu.png)File > Export OBJ... or Export GLB... writes the mesh for use in Kosmos. 


Things to try next: select the top cap in Face mode and press **Inset** then **Extrude** with a negative distance to sink a hatch, give the hatch its own material (`hatch`) and a different colour, then carry on with the lander tutorial below.

<a id="tutorial"></a>

## Tutorial: a small lander

You will model a lander with a body, a tapered nose, two legs and one engine, and save it as a ship.

  1. **Start.** Run `kosmoscad`. The four views show an empty grid. Press **1** for Object mode.
  2. **Body.** Add > Cylinder, radius 2, height 3, 24 segments. Click the Front view and press **F** to frame it.
  3. **Nose.** Add > Cone / frustum with a base radius of 2 and a small top radius. Choose the Move tool (W) and drag the green arrow in the Front view until it sits on the cylinder. In the Transform section you can type the exact Y value instead.
  4. **Detail.** Select the body, press **4** for Face mode and click a side face in the perspective view. Press **Inset** , then **Extrude** with a distance of -0.1 to sink a hatch into the hull.
  5. **A leg.** Make a sketch: with the Front view active choose Add > Sketch, click a thin tapered outline (a long narrow triangle) and press Enter; depth 0.2. Draw it already to one side of the centre line (for example at X = 2.5) so it does not need moving afterwards.
  6. **The other leg.** With the leg selected press **Mirror X**. The mirrored leg is added to the same object on the other side.
  7. **Materials.** Press 1, select the body, then in Materials and UVs press **New material** named `hull`, pick a colour and press **Assign to selection**. Do the same for the legs with a material named `legs`. Use Box UV project if you add a texture, then press **Z** until the view is textured and **U** to slide the image into place in the UV editor.
  8. **Engine.** Add > Equipment > Engine. Drag it to the bottom of the body in the Front view. The arrow shows the thrust direction (the arrow is the thrust direction, +Z by default, which pushes the ship towards its nose at -Z) and set thrust and Isp in Equipment properties, then **Apply properties**. Add Gear markers under each leg the same way.
  9. **Save.** Create an addon folder with an `addon.json` and a minimal `ship.json` containing a `"name"` and one stage (the quickest way is to copy a shipped addon and open its `ship.json`), choose File > Save ship.json as... and point it at your folder. Meshes go to `models/`.
  10. **Fly it.** Run Kosmos, pick your ship, or `./kosmos --ship "Your ship name" --orbit`.



<a id="trouble"></a>

## Troubleshooting

  * **"Could not open ... this is not a ship.json"** : the file has no `"stages"` list. Open the ship's own `ship.json`.
  * **Nothing appears after opening a ship:** its parts use GLB models or generated props, which KosmosCAD does not show. Import an OBJ, or edit the OBJ version.
  * **A boolean looks wrong:** flip normals, weld near-duplicate vertices and make sure the objects are closed meshes.
  * **OpenGL 3.2 is required:** update your graphics driver.


