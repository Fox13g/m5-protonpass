#ifndef JSON_TRANSFORMER_H
#define JSON_TRANSFORMER_H

#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <Models/Category.h>
#include <Models/Entry.h>


class JsonTransformer {
public:
    std::string emptyJsonStructure();

    std::string toJson(const std::vector<Entry>& vault);
    std::vector<Entry> fromJsonToEntries(const std::string& jsonContent);
    std::string toJson(const std::vector<Category>& categories);
    std::vector<Category> fromJsonToCategories(const std::string& jsonContent);
    std::string mergeEntriesAndCategoriesToJson(const std::vector<Entry>& entries, const std::vector<Category>& categories);
};

#endif // JSON_TRANSFORMER_H
