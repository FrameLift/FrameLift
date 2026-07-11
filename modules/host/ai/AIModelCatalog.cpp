#include "AIModelCatalog.h"

namespace hostai
{
const std::vector<CatalogModel>& BuiltinModelCatalog()
{
    static const std::vector<CatalogModel> models = {
        {
            .id = "qwen3-vl-4b",
            .name = "Qwen3-VL 4B Instruct (Q4_K_M)",
            .quant = "Q4_K_M",
            .modelUrl = "https://huggingface.co/unsloth/Qwen3-VL-4B-Instruct-GGUF/resolve/main/"
                        "Qwen3-VL-4B-Instruct-Q4_K_M.gguf",
            .projectorUrl = "https://huggingface.co/unsloth/Qwen3-VL-4B-Instruct-GGUF/resolve/main/mmproj-F16.gguf",
            .recommended = true,
        },
        {
            .id = "smolvlm-256m",
            .name = "SmolVLM 256M Instruct (Q8_0)",
            .quant = "Q8_0",
            .modelUrl = "https://huggingface.co/ggml-org/SmolVLM-256M-Instruct-GGUF/resolve/main/"
                        "SmolVLM-256M-Instruct-Q8_0.gguf",
            .modelSha256 = "2a31195d3769c0b0fd0a4906201666108834848db768af11de1d2cef7cd35e65",
            .modelSize = 175054528,
            .projectorUrl = "https://huggingface.co/ggml-org/SmolVLM-256M-Instruct-GGUF/resolve/main/"
                            "mmproj-SmolVLM-256M-Instruct-Q8_0.gguf",
            .projectorSha256 = "7e943f7c53f0382a6fc41b6ee0c2def63ba4fded9ab8ed039cc9e2ab905e0edd",
            .projectorSize = 103769856,
        },
    };
    return models;
}
} // namespace hostai
