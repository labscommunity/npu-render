#pragma once
#include <cstdint>
namespace nr::registered_lab {
// D3D10/11 filter encodings. Every admitted SRV exposes exactly one mip, so
// the mip interpolation bit cannot change its result. Minification and
// magnification must still agree; comparison and anisotropic modes are distinct.
inline bool single_mip_point_filter(uint32_t filter){return filter==0 || filter==1;}
inline bool single_mip_linear_filter(uint32_t filter){return filter==20 || filter==21;}
inline bool single_mip_filter_supported(uint32_t filter){return single_mip_point_filter(filter) || single_mip_linear_filter(filter);}
}
