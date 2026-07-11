#pragma once

#include <string>
#include <vector>

namespace hostai
{
struct CatalogModel
{
    std::string id;
    std::string name;
    std::string quant;
    std::string modelUrl;
    std::string modelSha256;
    long long modelSize = 0;
    std::string projectorUrl;
    std::string projectorSha256;
    long long projectorSize = 0;
    bool recommended = false;
};

const std::vector<CatalogModel>& BuiltinModelCatalog();
} // namespace hostai
