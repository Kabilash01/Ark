#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

namespace ark {

// Reads llama-server's local runtime metadata without sending image contents.
Json inspect_vision_runtime(Transport transport, std::string_view base_url, std::string api_key = {});

} // namespace ark
