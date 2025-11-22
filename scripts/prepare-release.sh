#!/bin/bash
# Script to prepare ThemisDB for release packaging
# This script updates version numbers across all packaging files

set -e

VERSION=$1

if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 1.0.1"
    exit 1
fi

# Remove 'v' prefix if present
VERSION="${VERSION#v}"

echo "Preparing ThemisDB v${VERSION} for release packaging..."

# Update Debian changelog
echo "Updating debian/changelog..."
DEBFULLNAME="ThemisDB Team"
DEBEMAIL="info@themisdb.org"
export DEBFULLNAME DEBEMAIL

if command -v dch >/dev/null 2>&1; then
    dch -v "${VERSION}-1" "Release version ${VERSION}"
else
    echo "Warning: 'dch' command not found. Please update debian/changelog manually."
fi

# Update RPM spec
echo "Updating themisdb.spec..."
sed -i "s/^Version:.*/Version:        ${VERSION}/" themisdb.spec

# Update PKGBUILD
echo "Updating PKGBUILD..."
sed -i "s/^pkgver=.*/pkgver=${VERSION}/" PKGBUILD
sed -i "s/^pkgrel=.*/pkgrel=1/" PKGBUILD

# Update Chocolatey nuspec
echo "Updating packaging/chocolatey/themisdb.nuspec..."
sed -i "s|<version>.*</version>|<version>${VERSION}</version>|" packaging/chocolatey/themisdb.nuspec

# Update WinGet manifests
echo "Updating WinGet manifests..."
WINGET_DIR="packaging/winget/manifests/t/ThemisDB/ThemisDB"
if [ -d "${WINGET_DIR}" ]; then
    # Create new version directory
    mkdir -p "${WINGET_DIR}/${VERSION}"
    
    # Copy and update manifests
    for file in "${WINGET_DIR}"/*/ThemisDB.ThemisDB*.yaml; do
        [ -f "$file" ] || continue
        basename_file=$(basename "$file")
        sed "s/PackageVersion: .*/PackageVersion: ${VERSION}/" "$file" > "${WINGET_DIR}/${VERSION}/${basename_file}"
    done
fi

# Update Homebrew formula
echo "Updating packaging/homebrew/themisdb.rb..."
sed -i "s|url \".*v.*/.*\"|url \"https://github.com/makr-code/ThemisDB/archive/refs/tags/v${VERSION}.tar.gz\"|" packaging/homebrew/themisdb.rb

# Update CMakeLists.txt version
echo "Updating CMakeLists.txt..."
sed -i "s/project(Themis VERSION .* LANGUAGES CXX)/project(Themis VERSION ${VERSION} LANGUAGES CXX)/" CMakeLists.txt

# Update vcpkg.json version
echo "Updating vcpkg.json..."
sed -i "s/\"version\": \".*\"/\"version\": \"${VERSION}\"/" vcpkg.json

echo ""
echo "Version updated to ${VERSION} in all packaging files."
echo ""
echo "Next steps:"
echo "1. Review changes: git diff"
echo "2. Update CHANGELOG.md with release notes"
echo "3. Commit changes: git add . && git commit -m 'Bump version to ${VERSION}'"
echo "4. Create git tag: git tag -a v${VERSION} -m 'Release version ${VERSION}'"
echo "5. Push changes: git push && git push --tags"
echo "6. Create GitHub release to trigger package builds"
echo ""
echo "To calculate source tarball SHA256:"
echo "  wget https://github.com/makr-code/ThemisDB/archive/v${VERSION}.tar.gz"
echo "  sha256sum v${VERSION}.tar.gz"
echo "  # Update the hash in PKGBUILD and packaging/homebrew/themisdb.rb"
