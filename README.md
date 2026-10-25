# ASS Graphics

Windows x64 graphics/UI library, C++20, version 1.4.4 / C ABI 1. Build one native runtime DLL containing the UI/rendering modules, private B612 Regular/Bold fonts, compiled shaders and static C/C++ runtime. Optional .NET 10 Windows/WinForms bindings use the same native renderer.

This repository contains implementation source, public headers, shader/resource inputs, required font assets/licenses, build definitions and source examples. Compiled SDKs, binaries, tests, testing tools, logs, local automation and investigation records are intentionally excluded.

## Components

| Application need | Classes / mechanisms |
|---|---|
| Basic native utility | Context and Window: lifetime, message loop, DPI, placement and resizing |
| Tray connector | Tray: show/hide, close-to-tray, explicit exit, single-instance activation and Explorer recovery |
| Common appearance | Theme and privately registered B612 Regular/Bold |
| Settings/readout UI | Control: labels, numeric edits, checkboxes, sliders with values/units, scrolling, tabs and keyboard navigation |
| Copyable live status | Native status control and WinForms StatusTextBox: unchanged-text suppression and deferred updates during selection |
| Configuration storage | Settings: typed UTF-8 sections/values, defaults, diagnostics, temporary replacement and backups |
| Instrument/schematic drawing | Canvas and Viewport: Direct2D/DirectWrite, batched geometry/text/images, paths, transforms, clipping and PNG export |
| Telemetry history | Chart: panels/series, legends/units, linear and symmetric-log scales, patterns and missing-data gaps |
| Simple orientation/platform preview | CPU wire projection plus Canvas lines |
| Solid engineering 3D | Scene: Direct3D11 mesh/camera/depth, wireframe, lighting, blur and shared Direct2D overlays |

The application owns simulator protocols, data acquisition, units, coordinate conversion, physical limits, settings meaning and scene geometry. The DLL contains generic mechanisms. Native API comments in include/ass_graphics define coordinates, bounds and ownership.

## Build native Debug and Release

Install Visual Studio 2026 Desktop C++ tools, a Windows SDK including FXC, and CMake 3.25+. The native API floor is Windows 10 1703 because of DirectWrite5; execution on Windows 10 has not been qualified. Windows 11 builds use the system graphics APIs.

Choose an external build/work directory. On the maintained NWITTJE-MAIN workspace, use an actual task-owned O: cleanup lease; no E: toolkit is required. Other machines can choose their own external work directory.

~~~powershell
$env:ASS_GRAPHICS_WORK = 'O:\psx\bin\.work\<owned-task>\graphics'
cmake --preset vs2026-x64
cmake --build --preset debug
cmake --build --preset release
cmake --install "$env:ASS_GRAPHICS_WORK/native" --config Debug --prefix "$env:ASS_GRAPHICS_WORK/install/Debug"
cmake --install "$env:ASS_GRAPHICS_WORK/native" --config Release --prefix "$env:ASS_GRAPHICS_WORK/install/Release"
~~~

No Python, donor checkout or downloaded runtime asset is needed for this source-only build. Font hashes are checked during configuration. HLSL compiles during the build; full matching PDBs are installed for both configurations. The default build includes native basic, tray and gallery source examples. Set ASS_GRAPHICS_BUILD_EXAMPLES=OFF when only the DLL is needed.

The installed prefix contains bin/ass_graphics_lib.dll, lib/ass_graphics_lib.lib, public headers, CMake exports, symbols, source bindings and licenses. Use separate Debug/Release prefixes and keep their matching files together.

## Native integration

~~~cmake
find_package(AssGraphics 1.4.4 CONFIG REQUIRED)
add_executable(MyTool WIN32 main.cpp)
target_compile_features(MyTool PRIVATE cxx_std_20)
target_link_libraries(MyTool PRIVATE ass::graphics)
~~~

Set CMAKE_PREFIX_PATH to the matching installation prefix. Include ass_graphics/graphics.hpp for move-only C++ RAII classes, or ass_graphics/ass_graphics.h for the C ABI. Deploy the matching native DLL beside the executable and distribute the required notices. The import library, headers and PDB are development/debugging artifacts.

examples/basic.cpp shows a minimal window. examples/tray.cpp adds tray lifetime. examples/gallery.cpp uses synthetic 2D/chart/3D data and controls.

## C# / WinForms

Install .NET 10 SDK to build optional managed assemblies and the source gallery:

~~~powershell
dotnet build examples/ManagedGallery/ManagedGallery.csproj -c Debug --artifacts-path "$env:ASS_GRAPHICS_WORK/managed"
dotnet build examples/ManagedGallery/ManagedGallery.csproj -c Release --artifacts-path "$env:ASS_GRAPHICS_WORK/managed"
~~~

To deploy one ASS runtime DLL, compile binding source into your own .NET 10 Windows x64 application:

~~~xml
<PropertyGroup>
  <TargetFramework>net10.0-windows</TargetFramework>
  <AssGraphicsUseWinForms>true</AssGraphicsUseWinForms>
  <AssGraphicsPackage>C:\path\to\matching\install\Release</AssGraphicsPackage>
</PropertyGroup>
<Import Project="$(AssGraphicsPackage)\bindings\Ass.Graphics.Source.props" />
~~~

The import includes the native DLL in build/publish output. Omit AssGraphicsUseWinForms for core bindings alone. Alternatively reference the optional Ass.Graphics and Ass.Graphics.WinForms assemblies; do not also import their source. Managed applications retain their normal .NET runtime requirements.

## Ownership and update rules

Create UI objects on the owning UI thread. Keep Context and caller-owned parent HWNDs alive until their children are disposed. Destroy C++ wrappers or dispose C# objects on that same UI thread. Stop/join producers before destruction, and defer destruction from callbacks until the callback returns. Callbacks must not throw across the ABI.

Only documented canvas/chart/status snapshot submissions are worker-safe. They retain bounded latest snapshots; acquisition history belongs to the application. Submit arrays in batches. Mesh, camera, window/control and export operations use the owning thread. GPU readback/export is synchronous and belongs outside a live rendering loop.

Opaque C handles have explicit ownership. No STL objects, exceptions or allocator ownership cross the ABI. Never free a borrowed library resource in the application. A consumer's CRT setting is independent of the DLL's static CRT.

## Scope and licenses

Scene is a small engineering renderer: no scene graph, model importer, material system, geodesy, physics or transparent-object sorting. Chart renders supplied data and validity; it is not a telemetry database. Settings does not migrate a donor application's schema. Existing projects are not migrated by this repository.

B612 font files are required resource inputs, not compiled application binaries. They remain under the SIL Open Font License in licenses/B612-OFL.txt. licenses/NOTICE.txt covers attribution and the in-house source's proprietary status. No new open-source license is granted merely by publishing this repository.

This source publication does not establish Windows 10, physical mixed-monitor, simulator or hardware acceptance, and makes no new performance claim.