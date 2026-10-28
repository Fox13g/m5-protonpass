#include "CategoryService.h"


bool CategoryService::validateCategory(const Category& category) const {
    return !category.getName().empty();
}

void CategoryService::addCategory(const Category& category) {
    if (!validateCategory(category)) {
        throw std::runtime_error("Invalid category: Name is required.");
    }
    repository.addCategory(category);
}

void CategoryService::updateCategory(size_t index, const Category& updatedCategory) {
    auto existingCategory = repository.findCategoryByIndex(index);
    if (existingCategory.empty()) {
        throw std::runtime_error("Category not found.");
    }
    repository.updateCategory(index, updatedCategory);
}

Category CategoryService::findCategoryByIndex(size_t index) const {
    auto category = repository.findCategoryByIndex(index);
    if (category.empty()) {
        return Category(); // return empty model
    }
    return category;
}

std::vector<Category> CategoryService::getAllCategories() const {
    return repository.getAllCategories();
}

void CategoryService::setCategories(std::vector<Category>& cats) {
    repository.setCategories(cats);
}

std::vector<Category> CategoryService::sortCategoriesByName() const {
    auto categories = repository.getAllCategories();
    std::sort(categories.begin(), categories.end(), [](const Category& a, const Category& b) {
        return a.getName() < b.getName();
    });
    return categories;
}
