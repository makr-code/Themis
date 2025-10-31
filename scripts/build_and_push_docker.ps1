param(
  [string]$Repo = "themisdb/themis",
  [string]$Triplet = "x64-linux",
  [string]$Tag = "latest"
)

Write-Host "Building Docker image for triplet=$Triplet tag=$Tag"

docker build --build-arg VCPKG_TRIPLET=$Triplet -t $Repo:$Tag-$Triplet .

if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "Pushing $Repo:$Tag-$Triplet"
docker push $Repo:$Tag-$Triplet

# Optionally also push unsuffixed tag for single-arch usage
if ($Tag -eq "latest") {
  Write-Host "Tagging also $Repo:latest (single-arch)"
  docker tag $Repo:$Tag-$Triplet $Repo:latest
  docker push $Repo:latest
}
