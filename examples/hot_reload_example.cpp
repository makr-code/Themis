// Example: Using the Release Manifest Service

#include "updates/release_manifest.h"
#include "updates/manifest_database.h"
#include "updates/hot_reload_engine.h"
#include <iostream>

using namespace themis;

int main() {
    // 1. Create a release manifest
    updates::ReleaseManifest manifest;
    manifest.version = "1.2.0";
    manifest.tag_name = "v1.2.0";
    manifest.release_notes = "Security fixes and performance improvements";
    manifest.is_critical = true;
    manifest.build_commit = "abc123";
    
    // Add files to the manifest
    updates::ReleaseFile file1;
    file1.path = "bin/themis_server";
    file1.type = "executable";
    file1.sha256_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    file1.size_bytes = 1024000;
    file1.platform = "linux";
    file1.architecture = "x64";
    file1.permissions = "0755";
    file1.download_url = "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/themis_server";
    
    manifest.files.push_back(file1);
    
    // Calculate manifest hash
    manifest.manifest_hash = manifest.calculateHash();
    
    // 2. Serialize to JSON
    auto manifest_json = manifest.toJson();
    std::cout << "Manifest JSON:\n" << manifest_json.dump(2) << "\n\n";
    
    // 3. Parse from JSON
    auto parsed_manifest = updates::ReleaseManifest::fromJson(manifest_json);
    if (parsed_manifest) {
        std::cout << "Successfully parsed manifest for version: " 
                  << parsed_manifest->version << "\n\n";
    }
    
    // 4. Example: Hot-reload workflow
    std::cout << "Hot-Reload Workflow Example:\n";
    std::cout << "1. Download release: POST /api/updates/download/1.2.0\n";
    std::cout << "2. Verify release:   POST /api/updates/apply/1.2.0 (verify_only=true)\n";
    std::cout << "3. Apply update:     POST /api/updates/apply/1.2.0\n";
    std::cout << "4. If needed:        POST /api/updates/rollback/rollback_xyz\n\n";
    
    // 5. Example manifest database usage
    std::cout << "Manifest Database Operations:\n";
    std::cout << "- Store manifest:    manifest_db->storeManifest(manifest)\n";
    std::cout << "- Retrieve manifest: manifest_db->getManifest(\"1.2.0\")\n";
    std::cout << "- List versions:     manifest_db->listVersions()\n";
    std::cout << "- Verify manifest:   manifest_db->verifyManifest(manifest)\n\n";
    
    // 6. Example hot-reload engine usage
    std::cout << "Hot-Reload Engine Operations:\n";
    std::cout << "- Download:          engine->downloadRelease(\"1.2.0\")\n";
    std::cout << "- Verify:            engine->verifyRelease(manifest)\n";
    std::cout << "- Apply:             engine->applyHotReload(\"1.2.0\")\n";
    std::cout << "- Rollback:          engine->rollback(rollback_id)\n";
    std::cout << "- List rollbacks:    engine->listRollbackPoints()\n";
    
    return 0;
}
