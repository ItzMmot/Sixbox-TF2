# ClickGUI image background

Open **Visuals → Menu → Background image**. Choose a file, or enter its path,
and enable the background. PNG, JPEG, BMP, GIF and TIFF are supported through
Windows Imaging Component. Animated files display their first frame.

Under Proton, Linux paths such as `/home/user/Pictures/background.png` work;
Windows paths such as `Z:\home\user\Pictures\background.png` also work.
Relative paths resolve against the client's configuration directory. **Reload**
reads an edited file again. **Remove** clears the selection and disables the image.
A failed replacement leaves the previous valid image available and shows an error.

| Setting | Purpose |
| --- | --- |
| Area | Menu body, content area, or the entire screen behind the menu |
| Fit | Cover, contain, stretch, or tile |
| Tint / opacity | Multiply the image color and adjust transparency |
| Blur | Actual Gaussian blur, from 0 to 16 |
| Scale / offsets / rotation | Position and orient the image |
| Mirror / flip | Reverse the horizontal or vertical texture coordinates |
| Brightness / contrast / saturation | Adjust the source colors |
| Dim / vignette | Darken the image and its edges |
| Parallax | Smooth movement following the mouse, from 0 to 40 pixels |
| Panels | Control menu panel opacity above the background |
| Top / bottom gradient | Independent color overlays, including alpha |

Settings persist through the normal configuration system. The feature starts
turned off and does not select an image automatically.

Decoding and color/blur processing run on a dedicated worker. Placement, tint,
opacity and overlays draw from the cached texture without decoding every frame.
Rapid processing changes are debounced and superseded work is cancelled. Texture
uploads happen on the rendering thread; a device reset releases the texture and
recreates it from retained pixels. Closing the file picker during client unload
stops its worker before hooks are removed.

Files are limited to 64 MB, 64 megapixels, and 16384 pixels per source dimension.
The longest cached dimension is capped at 2048 pixels, or 1024 with blur. Original
image dimensions remain available for consistent tile size. Transparent pixels
are blurred with premultiplied alpha to avoid dark fringes.

Validation: portable placement/filter tests cover all four fit modes, rotation,
extreme aspect ratios, coverage, mirroring, bounded tile counts, allocation-free
layout, Gaussian symmetry, transparent edges and cancellation. Native Windows WIC
tests exercise PNG alpha, JPEG, GIF, BMP, Unicode paths, resizing and invalid files
under Wine. In-game appearance, the native picker and device-reset recovery still
require interactive verification.
