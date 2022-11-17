#ifndef MODELANALYZER_H
#define MODELANALYZER_H

#include <iostream>
#include <filesystem>
#include <cstdio>
#include <set>
#include <vector>
#include <iterator>
#include <nlohmann/json.hpp>
#include "../Common/PathManager.h"
#include "onnx/shape_inference/implementation.h"
// #include "../../library/onnx/onnx.pb.h"

class GraphNode
{
    
public:
    /// @brief
    GraphNode();

    /// @brief
    /// @param node
    /// @param TotalParams
    /// @param idx
    GraphNode(onnx::NodeProto node, std::set<std::string> TotalParams = std::set<std::string>(), int index = -1);

    /// @brief
    /// @param node
    GraphNode(const GraphNode &node);

    /// @brief
    /// @param node
    /// @return
    bool operator==(GraphNode &node);

    /// @brief
    /// @return
    bool IsConvergeNode();

// private:
    std::string name;
    std::string type;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> dependencies_inputs;
    std::vector<std::string> dependencies_outputs;
    std::set<std::string> params;
    nlohmann::json input_info;
    int idx;
};

class ModelAnalyzerIterator
{
    public:
        /// @brief 
        /// @param p 
        ModelAnalyzerIterator(GraphNode* p);

        /// @brief 
        /// @param iter 
        /// @return 
        bool operator!=(const ModelAnalyzerIterator &iter);

                /// @brief 
        /// @param iter 
        /// @return 
        bool operator==(const ModelAnalyzerIterator &iter);

        /// @brief 
        /// @return 
        ModelAnalyzerIterator& operator++();

        /// @brief 
        /// @param  
        /// @return 
        ModelAnalyzerIterator operator++(int);

        GraphNode& operator * ();
    
    private:
        GraphNode* _ptr;
};

/// @brief
class ModelAnalyzer
{
public:
    typedef ModelAnalyzerIterator iterator;
    /// @brief
    /// @param model_name
    /// @param onnx_path
    ModelAnalyzer(std::string model_name, const std::filesystem::path &onnx_path = "");

    /// @brief
    /// @return
    bool Init();

    /// @brief
    /// @return
    const std::filesystem::path &GetModelPath() const;

    /// @brief
    /// @param enable
    void SetEnableCache(bool enable = true);

    /// @brief
    /// @param node
    /// @return
    bool EnableStart(GraphNode &node);

    /// @brief
    /// @return
    nlohmann::json LoadCache();

    // /// @brief
    // void RuntimeAnalyze();

    /// @brief
    /// @param raw_onnx_path
    /// @param new_onnx_path
    /// @param new_onnx_param_path
    /// @param start_node
    /// @param end_node
    /// @param print_error
    /// @return
    nlohmann::json ExtractModelByNode(std::filesystem::path raw_onnx_path, std::filesystem::path new_onnx_path, std::filesystem::path new_onnx_param_path,
                                      GraphNode start_node, GraphNode end_node, bool print_error = true);

    /// @brief
    void RecordDependency();

    /// @brief
    /// @param childs
    /// @return
    nlohmann::json SplitAndStoreChilds(std::vector<GraphNode> childs);

    /// @brief
    /// @param onnx_path
    /// @param params_path
    /// @param default_batch
    /// @return
    static nlohmann::json CreateParamsInfo(std::filesystem::path onnx_path, std::filesystem::path params_path, int default_batch = 15);

    /// @brief
    /// @return
    std::vector<GraphNode> GetConvergeNodes();

    /// @brief
    /// @return
    const std::vector<GraphNode> &GetAllNodes() const;

    /// @brief 
    /// @param i 
    /// @return 
    GraphNode& operator[](int i);

    /// @brief 
    /// @return 
    iterator begin();

    /// @brief 
    /// @return 
    iterator end();

    int size() const;

private:
    /// @brief
    std::string modelName;

    /// @brief
    std::filesystem::path onnxPath;

    /// @brief
    OnnxPathManager manager;

    /// @brief
    std::vector<GraphNode> nodes;

    /// @brief
    GraphNode start_node;

    /// @brief
    std::set<std::string> params;

    /// @brief
    bool use_cache;
};






//Overload

/// @brief 
/// @param os 
/// @return 
std::ostream& operator<<(std::ostream &os, const GraphNode &node);

/// @brief 
/// @tparam T 
/// @param os 
/// @param v 
/// @return 
template<typename T>
std::ostream& operator<<(std::ostream &os, const std::vector<T> &v);

/// @brief 
/// @param os 
/// @param analyzer 
/// @return 
std::ostream& operator<<(std::ostream &os, const ModelAnalyzer &analyzer);






#endif // !MODELANALYZER_H