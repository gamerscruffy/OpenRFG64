@echo off
rem Builds PhysX 5.12 (third_party\physx-sdk, pinned commit b97365c6) for the 64-bit PhysX host and benchmark:
rem static CPU libraries plus PhysXGpu_64.dll (the SDK's supported pairing), static CRT (/MT), PhysX's "release"
rem configuration, with Ninja. Output: third_party\physx-build\release-gpu\.
rem
rem CUDA 12.8's nvcc accepts host compilers up to Visual Studio 2022, so the whole PhysX build uses the v143
rem toolset (14.44) that Visual Studio 2026 installs; the host, built with v145, links the libraries.
rem %1: GPU architectures with compiled kernels (default 75,86,89,120: GTX 16 / RTX 20, RTX 30, RTX 40, RTX 50, the
rem release set); %2: the architecture of the PTX that newer GPUs JIT (default 120); "sdk" = the SDK's own list (70 to
rem 120), much slower to build. A GPU older than every listed one gets no GPU PhysX (the host then uses CPU PhysX).
rem %3: PhysX's configuration: release (default), or profile, with PhysX's profiler zones, for measuring the host
rem (tools\build_host.ps1 -PhysXProfile); it builds into its own folder.
rem CPU-heavy and long (CUDA kernels): run it through the shared queue. Re-running rebuilds only what changed;
rem other architectures rebuild the GPU module.
setlocal
set "ARCHS=%~1"
if "%ARCHS%"=="" set "ARCHS=75,86,89,120"
set "PTX=%~2"
if "%PTX%"=="" set "PTX=120"
set "CONFIG=%~3"
if "%CONFIG%"=="" set "CONFIG=release"
for /f "tokens=1 delims=," %%a in ("%ARCHS%") do set "BASE=%%a"
rem Where PhysX's source and build live (OpenRFG64: outside the repository, so syncs never pack them):
rem   RFGPHYS_PHYSX_SRC  the PhysX checkout (default third_party\physx-sdk, the submodule)
rem   RFGPHYS_PHYSX_OUT  the build folder (default third_party\physx-build); %CONFIG%-gpu goes inside it
rem   RFGPHYS_PHYSX_CONFIGURE_ONLY=1  configure, then stop (checks the toolchain before the long CUDA build)
rem Visual Studio: the newest install with the v143 14.44 toolset (any edition, Build Tools included), from vswhere.
set "VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%v in (`"%VSWHERE%" -products * -prerelease -sort -property installationPath`) do (
    if not defined VS dir /b /ad "%%v\VC\Tools\MSVC\14.44*" >nul 2>&1 && set "VS=%%v"
)
if not defined VS (
    echo No Visual Studio with the MSVC v143 14.44 toolset: add Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64
    exit /b 1
)
set "CUDA=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
if not exist "%CUDA%\bin\nvcc.exe" (
    echo CUDA 12.8 not found at %CUDA%
    exit /b 1
)
echo Visual Studio: %VS%
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul || exit /b 1
set "CMAKE=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "PATH=%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%CUDA%\bin;%PATH%"
set "CUDA_PATH=%CUDA%"
cd /d "%~dp0.."
set "PATCH=%CD%\third_party\physx-sdk-rfgphys.patch"
set "SRC=%RFGPHYS_PHYSX_SRC%"
if "%SRC%"=="" set "SRC=%CD%\third_party\physx-sdk"
set "OUTBASE=%RFGPHYS_PHYSX_OUT%"
if "%OUTBASE%"=="" set "OUTBASE=%CD%\third_party\physx-build"
if not exist "%SRC%\physx\CMakeLists.txt" (
    echo PhysX source missing at %SRC%: git submodule update --init third_party/physx-sdk
    exit /b 1
)
rem Our changes to PhysX (third_party\physx-sdk-rfgphys.patch), applied to a fresh checkout.
git -C "%SRC%" apply --reverse --check "%PATCH%" >nul 2>&1
if errorlevel 1 (
    git -C "%SRC%" apply "%PATCH%" || exit /b 1
    echo applied third_party\physx-sdk-rfgphys.patch
)
set "OUT=%OUTBASE%\%CONFIG%-gpu"
set "ARCHDEFS=-DPX_GPU_SASS_ARCHS=%ARCHS% -DPX_GPU_PTX_ARCH=%PTX% -DCMAKE_CUDA_ARCHITECTURES=%BASE%"
if "%ARCHS%"=="sdk" set "ARCHDEFS=-UPX_GPU_SASS_ARCHS -UPX_GPU_PTX_ARCH -DCMAKE_CUDA_ARCHITECTURES=70"
echo PhysX GPU architectures: %ARCHS% (PTX %PTX%)
rem Configured every time, so that other architectures take effect.
"%CMAKE%" -S "%SRC%\physx" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% ^
    -DPX_GENERATE_GPU_PROJECTS=ON -DPX_GENERATE_STATIC_LIBRARIES=ON -DPX_GENERATE_GPU_STATIC_LIBRARIES=OFF ^
    -DCMAKE_CUDA_COMPILER="%CUDA%\bin\nvcc.exe" -DCUDAToolkit_ROOT="%CUDA%" %ARCHDEFS% ^
    -DNV_USE_STATIC_WINCRT=ON -DNV_USE_DEBUG_WINCRT=OFF ^
    -DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF -DPX_FLOAT_POINT_PRECISE_MATH=OFF ^
    -DPX_OUTPUT_LIB_DIR="%OUT%\lib" -DPX_OUTPUT_BIN_DIR="%OUT%\bin" -DPX_OUTPUT_DLL_DIR="%OUT%\bin" ^
    -DCMAKE_INSTALL_PREFIX="%OUT%\install" || exit /b 1
if "%RFGPHYS_PHYSX_CONFIGURE_ONLY%"=="1" (
    echo configured PhysX in %OUT% ^(RFGPHYS_PHYSX_CONFIGURE_ONLY=1: not built^)
    exit /b 0
)
"%CMAKE%" --build "%OUT%" || exit /b 1
"%CMAKE%" --install "%OUT%" || exit /b 1
echo built PhysX with GPU support into %OUT%
dir /s /b "%OUT%\install\*.lib" "%OUT%\install\*.dll" "%OUT%\install\*PxConfig.h" 2>nul
exit /b 0
