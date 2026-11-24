#pragma once

#include "ark/http.hpp"
#include "ark/message.hpp"

namespace ark {

class MaterialsProjectClient {
public:
    explicit MaterialsProjectClient(Transport transport, std::string api_key,
                                    std::string endpoint = "https://api.materialsproject.org");

    // Fetch summary documents by MP ID. A maximum of 1000 documents is requested.
    Json summaries(const std::vector<std::string>& material_ids,
                   const std::vector<std::string>& fields = {}, int limit = 100) const;

private:
    Transport transport_;
    std::string endpoint_;
    std::string api_key_;
};

} // namespace ark
