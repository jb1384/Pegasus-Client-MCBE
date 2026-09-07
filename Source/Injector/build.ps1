$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$output = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\Pegasus.exe'))
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /debug- /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "/out:$output" (Join-Path $PSScriptRoot 'Pegasus.cs')
if ($LASTEXITCODE -ne 0) { throw 'Injector compilation failed.' }
