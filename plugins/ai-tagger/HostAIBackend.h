#pragma once

#include "InferenceBackend.h"

class IAIInference;
class IAIModelManager;

namespace aitagger
{
std::unique_ptr<IInferenceBackend> CreateHostAIBackend(IAIInference* inference, IAIModelManager* models);
}
