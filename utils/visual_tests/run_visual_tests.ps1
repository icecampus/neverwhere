$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$CapturesDir = Join-Path $RepoRoot "utils\visual_tests\captures"
$ExePath = Join-Path $RepoRoot "_intermediate_64\Debug\PolygonalGeneratedLandscapePlayground.exe"
$PythonScript = Join-Path $RepoRoot "utils\visual_tests\check_mesh_preview.py"

if (-not (Test-Path $ExePath)) {
    throw "Build PolygonalGeneratedLandscapePlayground first: $ExePath"
}

New-Item -ItemType Directory -Force -Path $CapturesDir | Out-Null

Write-Host "Capturing visuals to $CapturesDir"
& $ExePath --capture-visuals $CapturesDir
if ($LASTEXITCODE -ne 0) {
    throw "PolygonalGeneratedLandscapePlayground --capture-visuals failed with exit code $LASTEXITCODE"
}

Write-Host "Running OpenCV checks"
python $PythonScript $CapturesDir
if ($LASTEXITCODE -ne 0) {
    throw "Visual checks failed"
}

Write-Host "Visual tests passed"
