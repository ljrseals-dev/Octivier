$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$JuceDir = if ($env:JUCE_DIR) { $env:JUCE_DIR } else { Join-Path $Root "JUCE" }
$BuildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { Join-Path $Root "build" }
$versionText = (& cmake --version | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or $versionText -notmatch 'version\s+(\d+)\.(\d+)') { throw "CMake 3.22+ is required." }
if ([int]$Matches[1] -lt 3 -or ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -lt 22)) { throw "CMake 3.22+ is required; found $versionText" }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw "Git is required to fetch JUCE." }
if (-not (Test-Path (Join-Path $JuceDir "CMakeLists.txt"))) {
  & git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git $JuceDir
  if ($LASTEXITCODE -ne 0) { throw "JUCE clone failed." }
}
$Protocol = Join-Path $Root "build-protocol"
& cmake -S $Root -B $Protocol -DOCTIVIER_PROTOCOL_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "Protocol configure failed." }
& cmake --build $Protocol --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw "Protocol build failed." }
& ctest --test-dir $Protocol -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Protocol tests failed." }
& cmake -S $Root -B $BuildDir "-DJUCE_DIR=$JuceDir" -DCMAKE_BUILD_TYPE=Release -DOCTIVIER_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "JUCE configure failed." }
& cmake --build $BuildDir --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw "JUCE build failed." }
& ctest --test-dir $BuildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "JUCE tests failed." }
