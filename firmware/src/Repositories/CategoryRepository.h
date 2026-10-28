#ifndef CATEGORY_REPOSITORY_H
#define CATEGORY_REPOSITORY_H

#include <vector>
#include <string>
#include <Models/Category.h>

class CategoryRepository {
private:
    std::vector<Category> categories;

public:
    void addCategory(const Category& category);
    const std::vector<Category>& getAllCategories() const;
    void setCategories(std::vector<Category>& cats);
    const Category& findCategoryByIndex(size_t index) const;
    bool deleteCategoryByIndex(size_t index);
    bool updateCategory(size_t index, const Category& updatedCategory);
};

#endif // CATEGORY_REPOSITORY_H
