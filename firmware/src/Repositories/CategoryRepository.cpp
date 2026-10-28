#include "CategoryRepository.h"
#include <algorithm>

void CategoryRepository::addCategory(const Category& category) {
    categories.push_back(category);
}

const std::vector<Category>& CategoryRepository::getAllCategories() const {
    return categories;
}

void CategoryRepository::setCategories(std::vector<Category>& cats) {
    categories = cats;
}

const Category& CategoryRepository::findCategoryByIndex(size_t index) const {
    static const Category emptyCategory;
    for (const auto& category : categories) {
        if (category.getIndex() == index) {
            return category;
        }
    }
    return emptyCategory;
}

bool CategoryRepository::deleteCategoryByIndex(size_t index) {
    auto it = std::remove_if(categories.begin(), categories.end(),
                             [index](const Category& category) { return category.getIndex() == index; });
    if (it != categories.end()) {
        categories.erase(it, categories.end());
        return true;
    }
    return false;
}

bool CategoryRepository::updateCategory(size_t index, const Category& updatedCategory) {
    for (auto& category : categories) {
        if (category.getIndex() == index) {
            category = updatedCategory;
            return true;
        }
    }
    return false;
}
