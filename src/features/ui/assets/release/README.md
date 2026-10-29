# Release UI assets

Downloaded from Figma file 7J8SYhLpybJgpxD3qFqL5u nodes 57:3552 and 57:3696.
app.png is the supplied image. SVG originals are retained; the PNG companions
are direct resvg-py rasterizations at the SVG root dimensions for native image
loaders. Do not redraw or substitute these assets.

Slots: titlebar app 16x16, close 10x10, checked 13x13;
notification app 28x28 and close 10x10. Windows embeds RCDATA 4701–4704;
macOS bundles these files under Resources/release.

To regenerate PNG companions: pip install resvg-py, then call
resvg_py.svg_to_bytes(svg_path=...) without dimension/style overrides.
