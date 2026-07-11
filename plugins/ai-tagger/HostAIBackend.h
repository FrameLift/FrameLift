#pragma once

#include "InferenceBackend.h"

class IAIInference;
class IAIImageQuestionScoring;
class IAIModelManager;

namespace aitagger
{
std::unique_ptr<IInferenceBackend> CreateHostAIBackend(
    IAIImageQuestionScoring* scoring, IAIInference* inference, IAIModelManager* models
);
}
