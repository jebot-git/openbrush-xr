# OpenBrush XR for de-xr

Source: [jebot-git/openbrush-xr](https://github.com/jebot-git/openbrush-xr).
The executable remains `openbrush-native`.

A native C++ OpenXR painting implementation based on Open Brush's editable
artwork format. The application uses OpenGL and FLTK directly. No Unity, Godot,
Unreal or other external game engine is built or linked. Multiplayer and cloud
functionality are outside scope. This remains an early implementation rather
than a claim of full Open Brush feature or visual parity.

## Build and run

Dependencies: C++17, CMake, Ninja, OpenXR loader/development headers, EGL/OpenGL,
libarchive, nlohmann JSON headers, FreeType, Fontconfig, libpng, libjpeg, GIO, Wayland development headers,
`wayland-scanner`, and the de-xr patched FLTK 1.4.5. Tests also use Python 3's
standard library; the optional desktop interaction test needs Xvfb.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFLTK_DIR=/opt/fltk-vr/toolkit/share/fltk
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
build/openbrush-native --demo
build/openbrush-native artwork.tilt
build/openbrush-native --probe
build/openbrush-native --xr --open artwork.tilt --save edited.tilt
```

The default FLTK frontend explicitly selects Wayland and does not fall back to
X11. X11 is available only with `-DOPENBRUSH_ENABLE_X11=ON` and an explicit
`--x11` launch. `--version` and `--probe` do not create a desktop window.
`--demo --render preview.ppm` renders an offscreen native GL diagnostic without
starting XR or opening a display.

Stage an optimized build as the regular user before installing system files:

```sh
DESTDIR="$PWD/build/stage" cmake --install build --prefix /usr/local
build/stage/usr/local/bin/openbrush-native --version
# After reviewing the staged files, install with administrative privileges:
sudo cmake --install build --prefix /usr/local
sudo update-desktop-database /usr/local/share/applications
sudo update-mime-database /usr/local/share/mime
```

The installation includes the desktop launcher and **Paint in XR** action, SVG
icon, `.tilt` MIME registration, command-line tool, licenses and provenance.
The launcher uses the default Wayland editor; its XR action runs standalone.
Installed binaries retain the selected patched FLTK library path. No service or
compositor restart is required.

## FLTK editor and shared drawing state

The desktop window provides a GPU viewport, tools, brush selection, native color
chooser, size/opacity controls, mirrored painting, layers, visibility, selection,
duplication, recoloring and selection transforms. File dialogs open and save
editable `.tilt` artworks asynchronously. Native FLTK widgets follow DomeD's
bitmap font and appearance changes. Real commands are exported to the universal
menu; the local bar collapses only while a menu host is available and restores
on host loss. Its hidden strip is reclaimed and keyboard shortcuts remain live.

Left drag applies the selected tool on the current view plane. Select and
Recolor pick strokes along the view ray. Right drag orbits; middle/Shift drag
pans; the mouse wheel zooms in the standard desktop direction. Selection is
shown with a yellow spatial bounding box. `F` fits the drawing, `R` resets the
view, Ctrl-Z/Ctrl-Shift-Z undo/redo, Ctrl-A selects all, Ctrl-D duplicates,
Delete removes selected strokes, and Ctrl-S saves. Mirror pairs and compound
selection edits undo as single commands.

**Start XR painting** (F6) starts a native XR worker using the same document and
tool settings as the FLTK editor. Palette changes apply while XR is running.
The model uses synchronized edits and immutable scene snapshots; neither
rendering thread holds the model lock while submitting GPU work. Completed
stroke meshes stay in GPU buffers until the document changes. Active strokes
and selection bounds update separately. Layers hidden in the editor are also
hidden in XR; hidden content remains in saved artworks.

Unsaved drawings are copied to a private per-process recovery file every five
seconds, including an in-progress stroke. A subsequent desktop launch offers
drawings from dead processes for recovery. Recovery opens as unsaved artwork
and requires a real save destination. Cancel/save/discard prompts protect
changes when opening, creating or closing drawings. Runtime data is under
`$XDG_STATE_HOME/openbrush-native` (normally `~/.local/state/openbrush-native`).

## Reference images on the canvas

Use **File → Import reference image...** to load a PNG or JPEG into a new layer.
The image appears at the current drawing plane, facing the current view, with
its aspect ratio preserved. Its layer is selected and labeled with the filename.
Paint over it normally, or select a different layer for separate artwork.
Reference images are underlays: painted strokes remain visible above them.

Select the image layer and use **Layer → Reference image properties...** to move
it along its plane, change its width in meters, or set opacity. The existing layer
visibility toggle hides the reference. **Layer → Remove reference image** removes
it. Import, placement/opacity changes and removal support Undo/Redo. Fit drawing
includes visible image corners. Images also appear as spatial planes in XR.

References are embedded as normalized PNG assets in `.tilt` saves and recovery
files. The source image can be moved or deleted after import. Their shared pixel
assets avoid repeated decoding and texture uploads while painting; import and
save run in background workers. PNG alpha is retained. Supported limits are
8192 pixels per side, 16 megapixels per image, 32 megapixels total and 128 images.
The native `DeXrReferenceImages` metadata extension stores placement, layer and
opacity; original Open Brush `ImageIndex` records are preserved independently
and are not yet loaded from external image libraries. These reference underlays
are excluded from glTF stroke-mesh export.

## Native XR path

The XR renderer uses real stereo projection layers, controller poses/actions,
a floor-aligned STAGE reference space where supported (LOCAL fallback), GPU
OpenGL rendering and a windowless EGL context.
Default overlay mode requires `XR_EXTX_overlay`, blends transparently behind
the artwork, and coexists with the running compositor. `--exclusive` explicitly
allows a primary session. The current Linux graphics path additionally requires
`XR_KHR_opengl_enable` and `XR_MNDX_egl_enable`.

Oculus Touch / Quest is the primary controller layout:

| Control | Action |
| --- | --- |
| Right trigger | Paint/use selected tool; firm pull to click palette |
| Left trigger | Temporary eraser |
| Either grip / both grips | Move/rotate canvas / rotate and scale |
| Left X / Y | Toggle palette / toggle eraser and previous tool |
| Right A / B | Undo / redo |
| Left stick horizontal / vertical | Previous/next brush / tool size |
| Right stick horizontal / vertical | Previous/next color / opacity |
| Left stick click | Toggle mirror |
| Right stick click | Show and recenter palette |

Stick adjustments work between strokes, with deadzones, directional sectors and
held horizontal repeat. A stick held while painting or grabbing must return to
neutral before changing tools. Vertical size adjustment is proportional (about
2x per second at full deflection); opacity changes by 50% per second. Short
haptic ticks confirm discrete actions. Palette gestures capture at light trigger
pressure and activate once per firm pull, so dragging off a button cannot paint.
The right Meta/system button is reserved for the runtime.

Touch Pro and Touch Plus bindings are enabled only when their corresponding
OpenXR extensions are advertised. The component paths follow the
[Khronos Touch Pro](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/fb/fb_touch_controller_pro.adoc)
and [Touch Plus](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/meta/meta_touch_controller_plus.adoc)
profiles. Vive, Index and simple-controller pose/trigger bindings remain available.
WiVRn currently supplies tracked poses using the simple profile; physical Touch
button gestures and haptics still need headset verification.
Stopping XR or changing a drawing stops only this application's XR worker;
there is no compositor, display-manager or WiVRn restart.

`--xr` runs without the desktop editor. Without `--save`, completed edits save
to `recovery.tilt` in the state directory, restored on the next standalone
launch with no input file. Saving runs on a dedicated background worker: edit
requests coalesce while a previous write is pending, and normal exit waits for
queued saves. Compression and disk I/O do not run on the XR frame thread.
Write failures are reported and may be retried by requesting another save; an
unresolved failure makes standalone mode exit unsuccessfully. A saved snapshot
contains hidden layers and records its matching revision atomically, so later
edits remain dirty. Input artwork is never silently replaced unless
explicitly selected as the save destination. `--frames N` bounds a runtime test.

The headset has a native spatial palette for tools, brushes, preset colors,
size/opacity, mirrored painting, layers, undo/redo, selection, duplication,
recolor, deletion and Save. It appears near the left hand in view and stays
anchored in the room. Point the right controller's aim ray and press its trigger
to activate a control. A click stays captured until release, so dragging off the
panel does not paint accidentally. Left X/menu toggles the palette; **Hide**
leaves a small **Tools** tab which can reopen it on simple controllers without
an application menu button. The ray and target marker remain projected onto
the panel.

Palette text uses the configured DomeD font face/style through an isolated
Fontconfig/FreeType loader; rasterized bitmap glyphs, colors, accent, opacity and
1x/2x layout track Appearance changes. The small panel image uploads only when
its contents change and is projected with OpenGL. No additional UI toolkit or
external engine is used. Save requests from the XR worker are marshalled to the
FLTK thread; standalone mode uses its selected save destination.

Squeeze either controller to move/rotate the canvas; squeeze both to rotate and
scale around their midpoint. On simple controllers, choose **Move** and use the
triggers as grabs instead. Grip transitions rebase without jumping. Navigation
is a session view transform: stored strokes and undo history are unchanged.
New painting and erasing map back through that transform, keeping the physical
tool size consistent. A controller-tip preview shows brush size or tool radius.
**Fit drawing** brings visible artwork in front of the viewer, and **Reset view**
restores the original placement. Focus/tracking loss ends a stroke; held triggers
must be released before painting resumes. The same guard applies on startup.

The production interaction path is covered by injected tracked-pose/action tests,
including simple-controller trigger grabs. Physical user gestures and the full
creative workflow still need manual verification. A headset-native file browser,
additional brush fidelity work remain
outstanding; FLTK dialogs provide file operations in desktop mode.

## glTF export

Choose **File → Export glTF...** from DomeD's universal menu (or the local menu
outside a menu-host session). Select `.gltf` for JSON with an embedded buffer or
`.glb` for binary glTF. Both produce one self-contained, atomically written file.
Export runs in the background and leaves the editable `.tilt` destination and
unsaved state unchanged. All document layers are exported, including hidden
layers; the temporary desktop/XR canvas navigation transform is excluded.

```sh
openbrush-native --open artwork.tilt --export-gltf artwork.glb
openbrush-native --demo --export-gltf example.gltf
openbrush-tool --export-gltf artwork.tilt artwork.gltf
```

The exporter uses glTF 2.0 and the same native brush meshes as the renderer,
including pressure-dependent widths, normals, per-point RGBA colors, alpha
blending and double-sided surfaces. Coordinates remain right-handed, Y-up,
in meters. Stroke nodes are grouped by layer; brush GUIDs, source indices and
group IDs are recorded in `extras`. glTF vertex colors are clamped to its legal
0–1 range; original stroke colors, including HDR values, remain in node `extras`.
Original `.tilt` colors remain unchanged. Light uses `KHR_materials_unlit`; other
brushes use rough, nonmetallic materials. Partial emission and original Unity
materials, brush textures and reference media are not reconstructed. Scene and
layer transforms are applied to exported mesh positions and normals. Export is a mesh interchange format; keep `.tilt` for editable
strokes and preserved source metadata. Unknown brushes use the native fallback.

The `.gltf` and `.glb` outputs follow the
[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/).
Tests independently decode buffer/accessor data and check bounds, normals,
indices, colors, alpha, layers, empty scenes, both CLIs and export failure
preservation. The FLTK interaction test checks asynchronous export and that it
does not mark the drawing saved. Sample exports also pass Khronos glTF Validator
with zero errors or warnings; that validator is not a runtime dependency.

## Original `.tilt` interoperability

Packed and loose-directory artworks retain source-local stroke coordinates.
Rendering, picking, painting, mirroring and selection edits apply the original
`SceneTransformInRoomSpace` and per-layer transforms in right-handed meters;
new or edited strokes are saved back into the appropriate layer space. Scene
navigation remains a separate session transform. Imported layer names and
visibility are respected, and visibility changes persist in original metadata.
Legacy layer transforms with zero scale follow Open Brush's identity recovery.

Untouched source positions/sizes retain their original float values on export,
preventing unit conversion drift. Original sketch version, extension presence,
missing random seeds, duplicate/unused brush-table entries, external model/image/video
references, opaque sketch/header
bytes, unknown metadata and nested attachments survive round trips. Existing
loose directories save through an atomic directory exchange; packed saves use
atomic file replacement. External media references are retained without being
silently discarded; **Help → Artwork information...** reports unsupported media
and strokes using fallback geometry. This is file-format interoperability, not
original Unity brush-material parity.

Independent tests construct v5/v6 files with sparse/unknown extensions, HDR
colors, unusual coordinates and extended container headers, then verify exact
sketch bytes across repeated saves. Manual verification also checks the upstream
**Credits** (1,128 strokes, 26,803 points) and **NoodlesCabin** (10,112 strokes,
339,898 points): both preserve their complete original `data.sketch` bytes and
all metadata on export. Those larger upstream artworks are verification inputs,
not redistributed application assets.

## Artwork and brush coverage

The `.tilt` reader supports packed containers and loose metadata/sketch files,
sketch versions 5/6, Unity left-handed decimeter to OpenXR right-handed meter
conversion, pressure/timestamps, brush GUIDs, stroke flags, scale, group and
layer IDs. Packed-file attachments, metadata fields and unknown binary
extensions are retained on save. Writes use a temporary file, sync and atomic
replacement. Use new output filenames when editing artwork with this prototype.

Eight native brush styles currently have distinct geometry/material behavior:
Wire, Flat, Marker, Ink, Light, Dots, Stars and Highlighter. These are native
approximations, not shader/texture parity with Unity Open Brush. Light is
unshaded; stamp brushes produce oriented dots/stars; Highlighter uses opacity.
Per-point replacement/multiply/add color extensions are interpreted. Unknown
brush GUIDs are preserved and rendered using the wire fallback. Referenced
models, original Open Brush image-library entries and videos, environments and original brush
materials/textures are not yet rendered; their metadata is retained. Additional
export formats beyond glTF are not implemented.

## Verification and provenance

Default CTest covers the real upstream `.tilt` fixture, an independent Python
reader, extension/metadata/attachment preservation, malformed input, atomic
save behavior, history, native colored GL output, editor transactions/layers,
concurrent snapshots and Wayland backend policy. Additional tests cover projected
palette hit/layout agreement, captured clicks, focus/tracking recovery, grip and
trigger navigation, pivot scaling, room-space tool sizes, light/2x Appearance,
malformed settings and CPU/GPU palette pixel agreement. Touch tests cover the
production binding table, extension gating, stick repeat/deadzones, stroke guards,
button shortcuts, haptic requests and firm-trigger palette activation.
Reference tests cover PNG/JPEG decoding, transparency, undo/redo, layer visibility,
embedded saves/recovery without the source file, GPU image orientation and painted
strokes over the image. Background-save tests block the writer to verify nonblocking requests, queue
coalescing, version guards, hidden-layer preservation, failed-write recovery and
shutdown drain. Build with the optional X11
backend to include a private-display/private-bus FLTK interaction test covering
pointer alignment, editing, menu host gain/loss, asynchronous file operations
and recovery. A native Wayland capture has also been inspected under DomeD.
A short WiVRn run on 2026-10-23 submitted the scene and palette in 14 stereo
frames out of 15, with valid poses for both hands. The runtime selected the
Khronos simple-controller profiles and supported STAGE space. Desktop and XR
can also run together. These checks did not restart the user's session.

`third_party/upstream.json` records the Open Brush revision, format sources and
fixture provenance. Its 83-entry reference brush catalog is not a list of
implemented brushes. The ignored upstream reference checkout is not a build or
installation dependency. `third_party/ui-adapters.json` records the separately
MIT-licensed de-xr appearance, bitmap-font, GPU-widget and menu adapters.
Open Brush-derived material and native code use Apache-2.0 (`LICENSE`, `NOTICE`).
The native EGL bootstrap is MIT-licensed from Ghostty XR via FLTerm XR; its
notice is in `third_party/LICENSE.native-graphics`.
