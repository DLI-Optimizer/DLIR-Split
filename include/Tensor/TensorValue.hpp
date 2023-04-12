#ifndef __TENSORVALUE_H__
#define __TENSORVALUE_H__

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <ostream>
#include <memory>
#include <random>
#include <vector>
#include <iostream>
#include "ValueInfo.h"

class TensorValueObject
{
public:
    /// @brief deep-copy from Ort::value to TensorValue
    /// @param value Ort::Value
    virtual void RecordOrtValue(Ort::Value &value)=0;

    /// @brief
    /// @param value ignore raw shape, override shape with Ort::Value.
    /// @param label if label=="---", will not delete raw label.
    virtual void RecordOrtValueIgnoreShape(Ort::Value &value, std::string label = "---")=0;

    /// @brief create random datas
    /// @param min <= data[x]
    /// @param max > data[x]
    virtual void Random(int min = 0, int max = 1)=0;

    /// @brief set label for tensor-info
    /// @param label usually label-name in model.
    virtual void SetLabel(std::string label = "")=0;

    // // template member function is not allow to be virtual
    // /// @brief get raw data
    // /// @return std::vector<T> object
    // // virtual const std::vector<T> &GetData() const=0;
    // // virtual const std::vector<T> &GetData() const=0;

    /// @brief bind to Ort::Value, it is similar to shallow-copy. if TensorValue change, Ort::value change together.
    /// @return
    virtual Ort::Value ToTensor() =0;

    /// @brief get tensor-info
    /// @return ValueInfo Info
    virtual const ValueInfo &GetValueInfo() const =0;

    /// @brief print Tensor-info
    // template<typename ValueType=float>
    void virtual Print(int64_t max_length = 30, bool print_tensor_info = true) const =0;

    virtual operator Ort::Value()=0;

    virtual ONNXTensorElementDataType GetDataElementType() =0;
};

/// @brief object to store tensor-info and datas
template <class T = float>
class TensorValue: public virtual TensorValueObject
{
public:
    /// @brief Init class and create random value.
    /// @param valueInfo shape and name Info
    /// @param initByRandom whether to init data by random
    /// @param allocator ort allocator
    /// @param memType memory type
    TensorValue(const ValueInfo &valueInfo, bool initByRandom = false, OrtAllocatorType allocator = OrtAllocatorType::OrtArenaAllocator, OrtMemType memType = OrtMemType::OrtMemTypeDefault);
    TensorValue(const TensorValue &value) = default;
    TensorValue() = delete;

    /// @brief deep-copy from Ort::value to TensorValue
    /// @param value Ort::Value
    virtual void RecordOrtValue(Ort::Value &value);

    /// @brief
    /// @param value ignore raw shape, override shape with Ort::Value.
    /// @param label if label=="---", will not delete raw label.
    virtual void RecordOrtValueIgnoreShape(Ort::Value &value, std::string label = "---");

    /// @brief create random datas
    /// @param min <= data[x]
    /// @param max > data[x]
    virtual void Random(int min = 0, int max = 1);

    /// @brief set label for tensor-info
    /// @param label usually label-name in model.
    virtual void SetLabel(std::string label = "");

    /// @brief get raw data
    /// @return std::vector<T> object
    const std::vector<T> &GetData() const;

    /// @brief bind to Ort::Value, it is similar to shallow-copy. if TensorValue change, Ort::value change together.
    /// @return
    virtual Ort::Value ToTensor();

    /// @brief get tensor-info
    /// @return ValueInfo Info
    virtual const ValueInfo &GetValueInfo() const;

    /// @brief print Tensor-info
    // template<typename ValueType=float>
    virtual void Print(int64_t max_length = 30, bool print_tensor_info = true) const;

    virtual operator Ort::Value();

    virtual ONNXTensorElementDataType GetDataElementType();

private:
    ValueInfo valueInfo;
    std::vector<T> data;
    OrtAllocatorType allocator;
    OrtMemType memType;
    ONNXTensorElementDataType elementType;
};

template <class T>
TensorValue<T>::operator Ort::Value()
{
    return this->ToTensor();
}

template <class T>
TensorValue<T>::TensorValue(const ValueInfo &valueInfo, bool initByRandom, OrtAllocatorType allocator, OrtMemType memType) : data(valueInfo.GetDataCount())
{
    this->valueInfo = valueInfo;
    this->allocator = allocator;
    this->memType = memType;
    this->elementType = valueInfo.GetType();

    if (initByRandom)
    {
        this->Random();
    }
}

template <class T>
ONNXTensorElementDataType TensorValue<T>::GetDataElementType()
{
    return this->elementType;
}

template <class T>
void TensorValue<T>::Random(int min, int max)
{
    switch (this->elementType)
    {
    case ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:;
    case ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:;
    case ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:;
    case ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:;
        if(max-min<10)
        {
            max=min+10;
        }
        break;
    default:
        break;
    }

    static std::default_random_engine engin(time(NULL));
    std::uniform_real_distribution<double> uniform_creator(min, max);

    if (this->elementType == ONNXTensorElementDataType::ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL)
    {
        for (int i = 0; i < this->data.size(); i++)
        {
            this->data[i] = (float)uniform_creator(engin) >= (max + min) / 2 ? true : false;
        }
    }
    else
    {
        for (int i = 0; i < this->data.size(); i++)
        {
            this->data[i] = (T)uniform_creator(engin);
        }
    }
}

template <class T>
void TensorValue<T>::RecordOrtValue(Ort::Value &value)
{
    // std::cout<<value.GetTensorMutableData<T>()<<std::endl;
    // std::cout<<this->data.data()<<std::endl;
    std::memcpy(this->data.data(), value.GetTensorMutableData<T>(), sizeof(T) * this->valueInfo.GetDataCount());
    // value.release();
    Ort::OrtRelease(value.release());
}

template <class T>
void TensorValue<T>::RecordOrtValueIgnoreShape(Ort::Value &value, std::string label)
{
    const Ort::TensorTypeAndShapeInfo &info = value.GetTensorTypeAndShapeInfo();
    if (label == "---")
    {
        label = this->valueInfo.GetName();
    }

    this->valueInfo = ValueInfo(label, info.GetShape(), info.GetElementType());
    this->RecordOrtValue(value);
}

template <class T>
Ort::Value TensorValue<T>::ToTensor()
{
    return Ort::Value::CreateTensor<T>(Ort::MemoryInfo::CreateCpu(allocator, memType), data.data(), data.size(), valueInfo.GetShape().data(), valueInfo.GetDimSize());
}

template <class T>
void TensorValue<T>::SetLabel(std::string label)
{
    this->valueInfo.SetName(label);
}

template <class T>
const std::vector<T> &TensorValue<T>::GetData() const
{
    return this->data;
}

template <class T>
const ValueInfo &TensorValue<T>::GetValueInfo() const
{
    return this->valueInfo;
}

template <class T>
void TensorValue<T>::Print(int64_t max_length, bool print_tensor_info) const
{
    if (print_tensor_info)
    {
        std::cout << this->valueInfo << std::endl;
    }

    int64_t length = max_length <= this->valueInfo.GetDataCount() ? max_length : this->valueInfo.GetDataCount();
    std::cout << "[";
    for (int64_t i = 0; i < length; i++)
    {
        if (i == 0)
        {
            std::cout << std::setiosflags(std::ios::fixed) << std::setprecision(6) << this->data[i];
        }
        else
        {
            std::cout << std::setiosflags(std::ios::fixed) << std::setprecision(6) << ", " << this->data[i];
        }
    }

    if (length < this->valueInfo.GetDataCount())
    {
        std::cout << " ...]" << std::endl;
    }
    else
    {
        std::cout << "]" << std::endl;
    }
}

#endif // __TENSORVALUE_H__