#pragma once

#include "core/types.hpp"
#include "render/marks_math.hpp"
#include "render/entity_math.hpp"

namespace ron::render {

void build_model_marks(const ModelCpu* models, int count, ModelMark* out) noexcept;

} // namespace ron::render
