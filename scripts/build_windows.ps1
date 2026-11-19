<#
.SYNOPSIS
  Compila la extensión "decide_amox" en Windows con MSVC + Ninja (sin make ni vcpkg).

.EXAMPLE
  # CPU (cualquier equipo)
  powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1

  # CUDA (laptop con GPU NVIDIA; requiere CUDA Toolkit 12.x o 13.x)
  powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Cuda

.NOTES
  Salidas:
    build\<config>\duckdb.exe                                          CLI con la extensión incluida
    build\<config>\extension\decide_amox\decide_amox.duckdb_extension            extensión cargable (LOAD)
#>
param(
    [switch]$Cuda,
    [string]$Config = "release",
    [string]$CudaArch = "",          # p.ej. "89" (RTX 40xx), "86" (RTX 30xx); vacío = 75;86;89
    [switch]$Portable,               # sin optimizaciones específicas de este CPU (para distribuir el binario)
    [int]$Jobs = 0
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# --- Visual Studio (2022 preferido: compatible con DuckDB v2.0) -------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vs) { $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
if (-not $vs) { throw "No se encontró Visual Studio / Build Tools con el componente C++ (VC.Tools.x86.x64)." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmakeDir = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake"
Write-Host "Visual Studio: $vs"

# --- Comprobaciones ----------------------------------------------------------------------------------------
foreach ($sub in "duckdb\CMakeLists.txt", "third_party\llama.cpp\CMakeLists.txt", "extension-ci-tools\makefiles") {
    if (-not (Test-Path (Join-Path $root $sub))) { throw "Falta el submódulo ($sub). Ejecuta: git submodule update --init --depth 1" }
}

$build = Join-Path $root "build\$Config"
$buildType = @{ release = "Release"; debug = "Debug"; reldebug = "RelWithDebInfo" }[$Config]
if (-not $buildType) { throw "Config desconocida: $Config" }

$flags = @(
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$buildType",
    "-DEXTENSION_STATIC_BUILD=1",
    "-DDUCKDB_EXTENSION_CONFIGS=$root\extension_config.cmake",
    "-DDUCKDB_EXPLICIT_VERSION=v2.0.0",              # versión exacta: la extensión carga en DuckDB 2.0.0 oficial
    "-DDUCKDB_CAPABILITIES=httplib loadable_extensions",  # sin repositorio local: INSTALL usa el repo oficial
    "-DDUCKDB_EXPLICIT_PLATFORM=windows_amd64",
    "-DENABLE_UNITTEST_CPP_TESTS=FALSE",
    "-DBUILD_UNITTESTS=FALSE",
    "-DENABLE_EXTENSION_AUTOLOADING=1",
    "-DENABLE_EXTENSION_AUTOINSTALL=1",
    "-DDECIDE_WITH_CUDA=$(if ($Cuda) {'ON'} else {'OFF'})",
    "-DDECIDE_NATIVE=$(if ($Portable) {'OFF'} else {'ON'})"
)
if ($Cuda -and $CudaArch) { $flags += "-DCMAKE_CUDA_ARCHITECTURES=$CudaArch" }

$jobsArg = if ($Jobs -gt 0) { "-j $Jobs" } else { "" }
$flagsText = ($flags | ForEach-Object { if ($_ -match '[ ;]') { "`"$_`"" } else { $_ } }) -join ' '
$vsInstaller = Split-Path -Parent $vswhere   # vcvars64.bat invoca vswhere.exe y lo busca en el PATH
# Un .bat con una instrucción por línea: en una sola línea, cmd expande %PATH% antes de que vcvars lo cambie.
$bat = Join-Path ([IO.Path]::GetTempPath()) "decide_amox_build_$PID.bat"
@"
@echo off
set "PATH=$vsInstaller;%PATH%"
call "$vcvars" >nul || exit /b 1
set "PATH=$cmakeDir\CMake\bin;$cmakeDir\Ninja;%PATH%"
cmake $flagsText -S "$root\duckdb" -B "$build" || exit /b 1
cmake --build "$build" --config $buildType $jobsArg || exit /b 1
"@ | Set-Content -Path $bat -Encoding ascii
Write-Host "Compilando ($Config, CUDA=$([bool]$Cuda)) en $build ..."
cmd /c "`"$bat`""
$code = $LASTEXITCODE
Remove-Item $bat -ErrorAction SilentlyContinue
$global:LASTEXITCODE = $code
if ($LASTEXITCODE -ne 0) { throw "La compilación falló (código $LASTEXITCODE)." }

$ext = Get-ChildItem -Path $build -Recurse -Filter "decide_amox.duckdb_extension" | Select-Object -First 1
Write-Host ""
Write-Host "OK"
Write-Host "  CLI:       $build\duckdb.exe"
if ($ext) { Write-Host "  Extensión: $($ext.FullName)" }
