# Application identity

The application icon is a raster export of `docs/assets/nova-mark.svg`, the same mark used
in the README. It contains PNG-backed 16, 24, 32, 48, 64, 128 and 256 px frames
in one Windows ICO container. The SVG remains the design source.

The checked-in ICO is a build input; normal builds do not need image tooling.
To regenerate it, make Node.js and the `sharp` package available to the script
(local module resolution or NODE_PATH), then run from the repository root:

```text
node tools/generate-icon.cjs docs/assets/nova-mark.svg studio/Assets/nova.ico
```

The Windows executable embeds the icon. The desktop window also selects it;
Axiom additionally uses its embedded resource for the notification-area icon.
Windows Explorer can cache old icons: use the newly built executable/package,
not an older portable preview. Icons do not imply a signed executable.

Local verification: native/WPF Release builds and 22 CTest cases passed.
`tools/check-icon.ps1` validated all seven ICO frames and an embedded executable
icon group; the extracted Release icon was visually inspected. Window/taskbar
rendering across Windows configurations has not been certified.
