#include "ModelCatalog.h"

namespace aitagger
{

std::vector<CatalogEntry> BuiltinCatalog()
{
    std::vector<CatalogEntry> c;

    // Recommended default. Larger, sharper answers; ~2.5 GB. sha256 left empty (not
    // pinned yet) so the download is accepted without verification.
    c.push_back(
        CatalogEntry{
            .id = "qwen3-vl-4b",
            .name = "Qwen3-VL 4B Instruct (Q4_K_M)",
            .quant = "Q4_K_M",
            .modelSize = 0,
            .modelUrl = "https://huggingface.co/unsloth/Qwen3-VL-4B-Instruct-GGUF/resolve/main/"
                        "Qwen3-VL-4B-Instruct-Q4_K_M.gguf",
            .modelSha256 = "",
            .mmprojSize = 0,
            .mmprojUrl = "https://huggingface.co/unsloth/Qwen3-VL-4B-Instruct-GGUF/resolve/main/mmproj-F16.gguf",
            .mmprojSha256 = "",
            .recommended = true,
        }
    );

    // Small, low-end-friendly (~270 MB total). Verified checksums.
    c.push_back(
        CatalogEntry{
            .id = "smolvlm-256m",
            .name = "SmolVLM 256M Instruct (Q8_0)",
            .quant = "Q8_0",
            .modelSize = 175054528,
            .modelUrl = "https://huggingface.co/ggml-org/SmolVLM-256M-Instruct-GGUF/resolve/main/"
                        "SmolVLM-256M-Instruct-Q8_0.gguf",
            .modelSha256 = "2a31195d3769c0b0fd0a4906201666108834848db768af11de1d2cef7cd35e65",
            .mmprojSize = 103769856,
            .mmprojUrl = "https://huggingface.co/ggml-org/SmolVLM-256M-Instruct-GGUF/resolve/main/"
                         "mmproj-SmolVLM-256M-Instruct-Q8_0.gguf",
            .mmprojSha256 = "7e943f7c53f0382a6fc41b6ee0c2def63ba4fded9ab8ed039cc9e2ab905e0edd",
            .recommended = false,
        }
    );

    return c;
}

} // namespace aitagger
