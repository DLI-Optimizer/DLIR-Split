#include "../../include/GPUAllocator/ModelExecutor.h"
#include "../../include/Common/JsonSerializer.h"
#include "../../include/Common/PathManager.h"
#include "../../include/Tensor/ModelInputCreator.h"
#include <vector>
#include <iostream>
#include <ctime>

ModelExecutor::ModelExecutor(std::string model_name, Ort::SessionOptions *session_opt, Ort::Env *env, int token_id, TokenManager *token_manager, std::mutex *gpu_mutex, std::condition_variable *deal_task) : modelName(model_name), sessionOption(session_opt), onnxruntimeEnv(env), todo(0), modelCount(0), tokenID(token_id), tokenManager(token_manager), gpuMutex(gpu_mutex), dealTask(deal_task)
{
    std::filesystem::path rawModelPath = OnnxPathManager::GetModelSavePath(modelName);
    this->executeTime = std::make_shared<std::vector<float>>();
    Ort::Session rawSession(*onnxruntimeEnv, rawModelPath.c_str(), *sessionOption);
    this->rawModelInfo = std::make_shared<ModelInfo>(rawSession, rawModelPath);

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

    // run test to skip cold-run and get run-time

    // test child-models
    {
        std::cout << "start to run " << modelName << " test." << std::endl;
        for (int i = 0; i < modelCount; i++)
        {
            std::vector<TensorValue<float>> input_Tensors;
            std::vector<Ort::Value> input_values;
            for (auto &info : modelInfos[i].GetInput().GetAllTensors())
            {
                input_Tensors.push_back(TensorValue(info, true));
            }

            for (auto &tensor : input_Tensors)
            {
                input_values.push_back(tensor);
            }

            // run to skip cold-run
            for (int k = 0; k < 3; k++)
            {
                this->sessions[i].Run(Ort::RunOptions{nullptr}, inputLabels[i].data(), input_values.data(), inputLabels[i].size(), outputLabels[i].data(), outputLabels[i].size());
            }

            // evaluate the time-cost
            clock_t start = clock();
            for (int k = 0; k < 3; k++)
            {
                this->sessions[i].Run(Ort::RunOptions{nullptr}, inputLabels[i].data(), input_values.data(), inputLabels[i].size(), outputLabels[i].data(), outputLabels[i].size());
            }
            this->executeTime->push_back((clock() - start) / 3.0 / CLOCKS_PER_SEC * 1000.0);
            // test child-models end.
        }
        // test raw-model
        {
            // run to skip cold-run
            std::vector<TensorValue<float>> raw_input_Tensors;
            std::vector<Ort::Value> raw_input_values;
            for (auto &info : modelInfos[0].GetInput().GetAllTensors())
            {
                raw_input_Tensors.push_back(TensorValue(info, true));
            }

            for (auto &tensor : raw_input_Tensors)
            {
                raw_input_values.push_back(tensor);
            }

            // skip cold-run
            for (int k = 0; k < 3; k++)
            {
                rawSession.Run(Ort::RunOptions{nullptr}, inputLabels[0].data(), raw_input_values.data(), inputLabels[0].size(), outputLabels[modelCount - 1].data(), outputLabels[modelCount - 1].size());
            }

            // evaluate the time-cost
            clock_t start_raw = clock();
            for (int k = 0; k < 3; k++)
            {
                rawSession.Run(Ort::RunOptions{nullptr}, inputLabels[0].data(), raw_input_values.data(), inputLabels[0].size(), outputLabels[modelCount - 1].data(), outputLabels[modelCount - 1].size());
            }
            this->modelExecuteTime = (clock() - start_raw) / 3.0 / CLOCKS_PER_SEC * 1000.0;

            // test raw-model end.
        }

        // evaluate end
    }

    // test end

    if (modelCount > 0)
    {
        // replace model-0 by raw-input
        std::vector<const char *> inputs;
        for (const ValueInfo &info : rawModelInfo->GetInput().GetAllTensors())
        {
            inputs.push_back(info.GetName().c_str());
        }
        this->inputLabels[0] = inputs;

        std::vector<const char *> outputs;
        for (const ValueInfo &info : rawModelInfo->GetOutput().GetAllTensors())
        {
            outputs.push_back(info.GetName().c_str());
        }
        this->outputLabels[modelCount - 1] = outputs;
    }

    // evaluate end
    std::cout << "run " << modelName << " test to end." << std::endl;
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
        this->current_task = this->task_queue.front();
    }

    current_task->_session = &this->sessions[this->todo];
    current_task->_input_labels = &this->inputLabels[this->todo];
    current_task->_output_labels = &this->outputLabels[this->todo];
}

bool ModelExecutor::RunOnce()
{
    this->LoadTask();
    if (this->current_task == nullptr)
    {
        std::cout << "warning: meet no input." << std::endl;
        return true;
    }

#ifndef ALLOW_GPU_PARALLEL
    std::unique_lock<std::mutex> lock(*gpuMutex);
    dealTask->wait(lock, [this]() -> bool
                   { return this->tokenManager->GetFlag() == tokenID; });
    // use token already
    // this->tokenManager->Release();

#endif // !ALLOW_GPU_PARALLEL
    clock_t start = clock();
    current_task->_input_datas = current_task->_session->Run(Ort::RunOptions{nullptr}, current_task->_input_labels->data(), current_task->_input_datas.data(), current_task->_input_labels->size(), current_task->_output_labels->data(), current_task->_output_labels->size());
    current_task->RecordTimeCosts(start, clock());

    // use token already
    this->tokenManager->Release();

#ifndef ALLOW_GPU_PARALLEL

    lock.unlock();
    dealTask->notify_all();

#endif // !ALLOW_GPU_PARALLEL

    this->ToNext();
    return false;
}

void ModelExecutor::AddTask(std::shared_ptr<std::map<std::string, std::shared_ptr<TensorValue<float>>>> datas, std::string tag)
{
    std::shared_ptr<Task> new_task = std::make_shared<Task>(this->modelName,this->modelExecuteTime, this->rawModelInfo, tag);
    new_task->SetInputs(datas);
    this->task_queue.Push(new_task);
}

void ModelExecutor::RunCycle()
{
    if (gpuMutex == nullptr || tokenManager == nullptr)
    {
        std::cout << "you give no device-mutex and token-manager info, system exit. This mode is only only allow while compiler with \"-DALLOW_GPU_PARALLEL\"" << std::endl;
        return;
    }
    while (true)
    {
        if(this->RunOnce())
        {
            break;
        }
    }
}

SafeQueue<std::shared_ptr<Task>> &ModelExecutor::GetResultQueue()
{
    return this->finish_queue;
}

SafeQueue<std::shared_ptr<Task>> &ModelExecutor::GetTaskQueue()
{
    return this->task_queue;
}

std::shared_ptr<std::vector<float>> ModelExecutor::GetExecuteTime()
{
    return this->executeTime;
}

int ModelExecutor::GetTokenID()
{
    return this->tokenID;
}

float &ModelExecutor::GetModelExecuteTime()
{
    return this->modelExecuteTime;
}

int ModelExecutor::GetChildModelCount()
{
    return this->modelCount;
}
