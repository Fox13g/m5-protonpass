#ifndef CATEGORY_SERVICE_H
#define CATEGORY_SERVICE_H

#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <Repositories/CategoryRepository.h>
#include <Models/Category.h>

class CategoryService {
private:
    CategoryRepository& repository;

public:
    // Constructeur
    CategoryService(CategoryRepository& repo) : repository(repo) {}

    // Validation
    bool validateCategory(const Category& category) const;

    // CRUD
    void addCategory(const Category& category);
    void updateCategory(size_t index, const Category& updatedCategory);
    Category findCategoryByIndex(size_t index) const;
    std::vector<Category> getAllCategories() const;
    void setCategories(std::vector<Category>& cats);

    // Méthodes utilitaires
    std::vector<Category> sortCategoriesByName() const;
};

#endif // CATEGORY_SERVICE_H
