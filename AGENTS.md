# Repository instructions

- Final binaries belong in the solution root's x64/Debug/ or x64/Release/ directories. For Win32, use root Win32/Debug/ or Win32/Release/ so no root-level Debug/Release directories are created.
- Intermediate files belong inside each project at $(ProjectDir)$(Platform)/$(Configuration)/. Never place final binaries inside a project's intermediate directory.
- Follow this layout in project settings, build commands, test scripts, and documentation. Do not override it.
- Use Dear ImGui for Pal16Edit's color picker. Never use Windows ChooseColor.
- Unsaved-change confirmations must use clear Japanese and explicit save, discard, and cancel labels.
