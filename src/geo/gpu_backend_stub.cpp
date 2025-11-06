#include "geo/spatial_backend.h"

namespace themis { namespace geo {

class GpuBatchBackendStub final : public ISpatialComputeBackend {
public:
    const char* name() const noexcept override { return "gpu_stub"; }
    bool isAvailable() const noexcept override {
#ifdef THEMIS_GEO_GPU_ENABLED
        return true;
#else
        return false;
#endif
    }
    SpatialBatchResults batchIntersects(const SpatialBatchInputs& in) override {
        SpatialBatchResults out;
        out.mask.assign(in.count, 0u);
        return out;
    }
};

} } // namespace themis::geo
