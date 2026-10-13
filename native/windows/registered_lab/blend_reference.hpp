#pragma once
// Independent scalar oracle for tests/replay, never a runtime fallback.
#include "output_merger.hpp"
#include <stdexcept>
namespace nr::raster {
// Source color/alpha are clamped normalized values; destination channels and
// alpha are bytes. Keep source and destination terms separate for FP32 checks.
template<class T> std::array<T,2> reference_blend_terms(BlendMode mode,T color,T alpha,T destination,T destination_alpha,T constant=T(1)){
    switch(mode){
    case BlendMode::source_copy:return {color*T(255),T(0)};
    case BlendMode::source_over:return {color*T(255),destination*(T(1)-alpha)};
    case BlendMode::destination_in:return {T(0),destination*alpha};
    case BlendMode::destination_out:return {T(0),destination*(T(1)-alpha)};
    case BlendMode::destination_over:return {color*(T(255)-destination_alpha),destination};
    case BlendMode::source_in:return {color*destination_alpha,T(0)};
    case BlendMode::source_out:return {color*(T(255)-destination_alpha),T(0)};
    case BlendMode::source_atop:return {color*destination_alpha,destination*(T(1)-alpha)};
    case BlendMode::destination_atop:return {color*(T(255)-destination_alpha),destination*alpha};
    case BlendMode::exclusive_or:return {color*(T(255)-destination_alpha),destination*(T(1)-alpha)};
    case BlendMode::plus:return {color*T(255),destination};
    case BlendMode::destination_copy:return {T(0),destination};
    case BlendMode::clear:return {T(0),T(0)};
    case BlendMode::component_coverage:return {color*(constant*T(255)),destination*(T(1)-color)};
    }
    throw std::invalid_argument("invalid reference blend mode");
}
}
