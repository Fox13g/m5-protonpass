#include "JsonTransformer.h"
#include <ArduinoJson.h>

std::string JsonTransformer::emptyJsonStructure() {
    DynamicJsonDocument doc(1024);

    JsonObject root = doc.to<JsonObject>();
    root.createNestedArray("entries");
    root.createNestedArray("categories");

    std::string jsonContent;
    serializeJson(doc, jsonContent);

    return jsonContent;
}

std::string JsonTransformer::toJson(const std::vector<Entry>& entries) {
    return mergeEntriesAndCategoriesToJson(entries, {});
}

std::vector<Entry> JsonTransformer::fromJsonToEntries(const std::string& jsonContent) {
    if (jsonContent.empty()) {
        return {};
    }

    // Use zero-copy in-place deserialization (char*) so strings are NOT duplicated in DynamicJsonDocument.
    // This reduces heap usage for 200 entries from ~130KB down to ~40KB on ESP32-S3.
    DynamicJsonDocument doc(49152);
    if (doc.capacity() == 0) {
        doc = DynamicJsonDocument(36864);
    }

    DeserializationError error = deserializeJson(
        doc,
        const_cast<char*>(jsonContent.data()),
        jsonContent.size()
    );
    if (error) {
        return {};
    }

    std::vector<Entry> entries;
    JsonArray entriesArray = doc["entries"].as<JsonArray>();
    entries.reserve(entriesArray.size());
    for (JsonObject entryObj : entriesArray) {
        Entry entry;
        entry.setId(entryObj["id"] | "");
        entry.setServiceName(entryObj["serviceName"] | "");
        entry.setUsername(entryObj["username"] | "");
        entry.setPassword(entryObj["password"] | "");
        entry.setCategoryIndex(entryObj["categoryIndex"] | 0);
        entry.setNotes(entryObj["notes"] | "");
        entry.setNotes2(entryObj["notes2"] | "");
        entry.setNotes3(entryObj["notes3"] | "");
        entry.setLink(entryObj["link"] | "");
        entry.setCreatedAt(entryObj["createdAt"] | 0L);
        entry.setUpdatedAt(entryObj["updatedAt"] | 0L);
        entry.setExpiresAt(entryObj["expiresAt"] | 0L);

        entries.push_back(std::move(entry));
    }

    return entries;
}

std::string JsonTransformer::toJson(const std::vector<Category>& categories) {
    DynamicJsonDocument doc(4096);

    JsonArray categoriesArray = doc.to<JsonArray>();
    for (const auto& category : categories) {
        JsonObject categoryObj = categoriesArray.createNestedObject();
        categoryObj["index"] = category.getIndex();
        categoryObj["name"] = category.getName();
        categoryObj["colorCode"] = category.getColorCode();
        categoryObj["iconPath"] = category.getIconPath();
    }

    std::string jsonContent;
    serializeJson(doc, jsonContent);
    return jsonContent;
}

std::vector<Category> JsonTransformer::fromJsonToCategories(const std::string& jsonContent) {
    if (jsonContent.empty()) {
        return {};
    }

    StaticJsonDocument<128> filter;
    filter["categories"] = true;
    DynamicJsonDocument doc(4096);

    DeserializationError error = deserializeJson(doc, jsonContent, DeserializationOption::Filter(filter));
    if (error) {
        return {};
    }

    std::vector<Category> categories;

    JsonArray categoriesArray = doc["categories"].as<JsonArray>();
    for (JsonObject categoryObj : categoriesArray) {
        Category category;
        category.setIndex(categoryObj["index"] | 0);
        category.setName(categoryObj["name"] | "");
        category.setColorCode(categoryObj["colorCode"] | "");
        category.setIconPath(categoryObj["iconPath"] | "");

        categories.push_back(category);
    }

    return categories;
}

std::string JsonTransformer::mergeEntriesAndCategoriesToJson(const std::vector<Entry>& entries, const std::vector<Category>& categories) {
    size_t docCapacity = std::max<size_t>(65536, entries.size() * 512 + 4096);
    DynamicJsonDocument doc(docCapacity);
    JsonObject root = doc.to<JsonObject>();

    JsonArray categoriesArray = root.createNestedArray("categories");
    for (const auto& category : categories) {
        JsonObject categoryObj = categoriesArray.createNestedObject();
        categoryObj["index"] = category.getIndex();
        categoryObj["name"] = category.getName();
        categoryObj["colorCode"] = category.getColorCode();
        categoryObj["iconPath"] = category.getIconPath();
    }

    JsonArray entriesArray = root.createNestedArray("entries");
    for (const auto& entry : entries) {
        JsonObject entryObj = entriesArray.createNestedObject();
        entryObj["id"] = entry.getId();
        entryObj["serviceName"] = entry.getServiceName();
        entryObj["username"] = entry.getUsername();
        entryObj["password"] = entry.getPassword();
        entryObj["categoryIndex"] = entry.getCategoryIndex();
        entryObj["notes"] = entry.getNotes();
        entryObj["notes2"] = entry.getNotes2();
        entryObj["notes3"] = entry.getNotes3();
        entryObj["link"] = entry.getLink();
        entryObj["createdAt"] = static_cast<long>(entry.getCreatedAt());
        entryObj["updatedAt"] = static_cast<long>(entry.getUpdatedAt());
        entryObj["expiresAt"] = static_cast<long>(entry.getExpiresAt());
    }

    std::string jsonContent;
    serializeJson(doc, jsonContent);

    return jsonContent;
}
