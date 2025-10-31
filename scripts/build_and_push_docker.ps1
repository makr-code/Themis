param(
  [string]$Repo = "yourdockerhub/themis",
  [string]$Triplet = "x64-linux",
  [string]$Tag = "latest"
)

Write-Host "Building Docker image for triplet=$Triplet tag=$Tag"

docker build --build-arg VCPKG_TRIPLET=$Triplet -t $Repo:$Tag-$Triplet .

if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "Pushing $Repo:$Tag-$Triplet"

docker push $Repo:$Tag-$Triplet
