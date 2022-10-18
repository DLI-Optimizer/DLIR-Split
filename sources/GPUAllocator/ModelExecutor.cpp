#include "../../include/GPUAllocator/ModelExecutor.h"
#include "../../include/Common/JsonSerializer.h"
#include "../../include/Common/PathManager.h"
#include <vector>
#include <iostream>
#include <ctime>

ModelExecutor::ModelExecutor(std::string model_name, Ort::SessionOptions *session_opt, Ort::Env *env, int token_id, TokenManager *token_manager, std::mutex *gpu_mutex, std::condition_variable *deal_task) : modelName(model_name), sessionOption(session_opt), onnxruntimeEnv(env), todo(0), modelCount(0), tokenID(token_id), tokenManager(token_manager), gpuMutex(gpu_mutex), dealTask(deal_task)
{
    std::filesystem::path rawModelPath = OnnxPathManager::GetModelSavePath(modelName);
    Ort::Session rawSession(*onnxruntimeEnv, rawModelPath.c_str(), *sessionOption);
    this->rawModelInfo = ModelInfo(rawSession, rawModelPath);

    std::filesystem::path modelSumParamsPath = OnnxPathManager::GetChildModelSumParamsSavePath(modelName);
    nlohmann::json json = JsonSerializer::LoadJson(modelSumParamsPath);
    int start = 0;
    while (json.contains(std::to_string(start)))
    {
        std::filesystem::path model_path = OnnxPathManager::GetChildModelSavePath(modelName, start);
        Ort::Session session(*onnxruntimeEnv, model_path.c_str(), *sessionOption);

        this->modelInfos.push_back(ModelInfo(session, model_path));
        this->sessions.push_back(std::move(session));
        this->modelCount += 1;
        start++;
    }

    for (auto &modelInfo : modelInfos)
    {
        std::vector<const char *> inputs;
        for (const ValueInfo &info : modelInfo.GetInput().GetAllTensors())
        {
            inputs.push_back(info.GetName().c_str());
        }
        this->inputLabels.push_back(inputs);

        std::vector<const char *> outputs;
        for (const ValueInfo &info : modelInfo.GetOutput().GetAllTensors())
        {
            outputs.push_back(info.GetName().c_str());
        }
        this->outputLabels.push_back(outputs);
    }
}

void ModelExecutor::ToNext()
{
    this->todo = (this->todo + 1) % this->modelCount;
    if (this->todo == 0)
    {
        this->current_task->SetOutputs(this->current_task->_input_datas);
        this->finish_queue.Emplace(std::move(this->task_queue.Pop()));
        this->current_task = nullptr;
    }
}

void ModelExecutor::LoadTask()
{
    if (this->todo == 0)
    {
        // how to block
        this->current_task = &this->task_queue.front();
    }

    current_task->_session = &this->sessions[this->todo];
    current_task->_input_labels = &this->inputLabels[this->todo];
    current_task->_output_labels = &this->outputLabels[this->todo];
}

void ModelExecutor::RunOnce()
{
    this->LoadTask();
    if (this->current_task == nullptr)
    {
        std::cout << "warning: meet no input." << std::endl;
        return;
    }

#ifndef ALLOW_GPU_Parallel

    std::unique_lock<std::mutex> lock(*gpuMutex);
    dealTask->wait(lock, [this]() -> bool
                   { return this->tokenManager->GetFlag() == tokenID; });
    // use token already
    this->tokenManager->Release();

#endif // !ALLOW_GPU_Parallel
    clock_t start = clock();
    current_task->_input_datas = current_task->_session->Run(Ort::RunOptions{nullptr}, current_task->_input_labels->data(), current_task->_input_datas.data(), current_task->_input_labels->size(), current_task->_output_labels->data(), current_task->_output_labels->size());
    current_task->RecordTimeCosts(clock() - start);

#ifndef ALLOW_GPU_Parallel

    lock.unlock();
    dealTask->notify_all();

#endif // !ALLOW_GPU_Parallel

    this->ToNext();
}

void ModelExecutor::AddTask(std::map<std::string, TensorValue<float>> &datas)
{
    Task new_task(this->modelName, &this->rawModelInfo);
    new_task.SetInputs(datas);
    this->task_queue.Emplace(std::move(new_task));
}

void ModelExecutor::RunCycle()
{
    while (true)
    {
        this->RunOnce();
    }
}

SafeQueue<Task> &ModelExecutor::GetResultQueue()
{
    return this->finish_queue;
}

SafeQueue<Task> &ModelExecutor::GetTaskQueue()
{
    return this->task_queue;
}
