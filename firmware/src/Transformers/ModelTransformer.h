#ifndef MODEL_TRANSFORMER_H
#define MODEL_TRANSFORMER_H

#include <vector>
#include <string>
#include <functional>
#include <Models/Category.h>
#include <Models/Entry.h>

class ModelTransformer {
public:
    // Template
    template <typename T>
    static std::vector<std::string> toStrings(const std::vector<T>& models, std::function<std::string(const T&)> extractor);

    // Surcharge pour Entry
    static std::vector<std::string> toStrings(const std::vector<Entry>& models);
    static std::vector<std::string> toStrings(const Entry& entry);

    // Surcharge pour Category
    static std::vector<std::string> toStrings(const std::vector<Category>& models);
};

#endif // MODEL_TRANSFORMER_H
