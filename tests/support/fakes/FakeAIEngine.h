#pragma once

#include <chrono>

namespace fakeai
{
void Reset();
void BlockInference(bool block);
bool WaitUntilRunning(std::chrono::milliseconds timeout);
int CreatedEngines();
int LastThreadLimit();
int QuestionBatches();
} // namespace fakeai
