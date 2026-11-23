#pragma once

#include "ark/coding.hpp"
#include "ark/reflection.hpp"
#include "ark/vector_memory.hpp"

namespace ark {

Json run_reflection_study(const Json& suite, const CodingAgent& coder, const ReflectionAgent& reflector,
                          const DockerSandbox& sandbox, Embeddings encoder,
                          const std::function<void(const Json&)>& snapshot);

} // namespace ark
