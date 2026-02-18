#include "rendering/pbr_push_constants.h"

#include <cassert>

int main() {
    using jupiter::rendering::PBRPushConstants;

    PBRPushConstants pc{};

    // Defaults should not have debug/disable flags set
    assert((pc.flags & PBRPushConstants::FLAG_DISABLE_IBL) == 0);

    // Roughness override sets flag and value
    pc.setRoughnessOverride(0.3f);
    assert(pc.roughnessOverride == 0.3f);
    assert((pc.flags & PBRPushConstants::FLAG_USE_ROUGHNESS_OVERRIDE) != 0);
    pc.clearRoughnessOverride();
    assert((pc.flags & PBRPushConstants::FLAG_USE_ROUGHNESS_OVERRIDE) == 0);

    // Metallic override sets flag and value
    pc.setMetallicOverride(0.7f);
    assert(pc.metallicOverride == 0.7f);
    assert((pc.flags & PBRPushConstants::FLAG_USE_METALLIC_OVERRIDE) != 0);
    pc.clearMetallicOverride();
    assert((pc.flags & PBRPushConstants::FLAG_USE_METALLIC_OVERRIDE) == 0);

    // Disable/enable IBL toggles the flag
    pc.disableIBL(true);
    assert((pc.flags & PBRPushConstants::FLAG_DISABLE_IBL) != 0);
    pc.disableIBL(false);
    assert((pc.flags & PBRPushConstants::FLAG_DISABLE_IBL) == 0);

    // Normal mapping flag toggle
    pc.disableNormalMapping(true);
    assert((pc.flags & PBRPushConstants::FLAG_DISABLE_NORMAL_MAPPING) != 0);
    pc.disableNormalMapping(false);
    assert((pc.flags & PBRPushConstants::FLAG_DISABLE_NORMAL_MAPPING) == 0);

    // Static size requirement is enforced in header; ensure still true at runtime
    static_assert(sizeof(PBRPushConstants) <= 128, "Push constants exceed Vulkan limit");

    return 0;
}







