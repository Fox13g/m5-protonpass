#ifndef ENTRY_H
#define ENTRY_H

#include <string>
#include <ctime>

class Entry {
private:
    std::string id;
    std::string serviceName;
    std::string username;
    std::string password;
    size_t categoryIndex;
    std::string notes;
    std::string notes2;
    std::string notes3;
    std::string link;
    time_t createdAt;
    time_t updatedAt;
    time_t expiresAt;

public:
    // Constructeurs
    Entry()
        : id(""), serviceName(""), username(""), password(""),
          categoryIndex(0), notes(""), notes2(""), notes3(""), link(""),
          createdAt(0), updatedAt(0), expiresAt(0) {}

    Entry(const std::string& serviceName, const std::string& username, const std::string& password, const std::string& notes)
        : id(""), serviceName(serviceName), username(username), password(password), 
          categoryIndex(0), notes(notes), notes2(""), notes3(""), link(""),
          createdAt(0), updatedAt(0), expiresAt(0) {}

    Entry(const std::string& id, const std::string& serviceName, const std::string& username, 
          const std::string& password, size_t categoryIndex)
        : id(id), serviceName(serviceName), username(username), password(password), 
          categoryIndex(categoryIndex), notes(""), notes2(""), notes3(""), link(""),
          createdAt(0), updatedAt(0), expiresAt(0) {}

    Entry(const std::string& id, const std::string& serviceName, const std::string& username, 
          const std::string& password, size_t categoryIndex, const std::string& link)
        : id(id), serviceName(serviceName), username(username), password(password), 
          categoryIndex(categoryIndex), notes(""), notes2(""), notes3(""), link(link),
          createdAt(0), updatedAt(0), expiresAt(0) {}

    // Accesseurs
    const std::string& getId() const { return id; }
    const std::string& getServiceName() const { return serviceName; }
    const std::string& getUsername() const { return username; }
    const std::string& getPassword() const { return password; }
    size_t getCategoryIndex() const { return categoryIndex; }
    const std::string& getNotes() const { return notes; }
    const std::string& getNotes2() const { return notes2; } 
    const std::string& getNotes3() const { return notes3; }
    const std::string& getLink() const { return link; }
    time_t getCreatedAt() const { return createdAt; }
    time_t getUpdatedAt() const { return updatedAt; }
    time_t getExpiresAt() const { return expiresAt; }

    // Mutateurs
    void setId(const std::string& newId) { id = newId; updatedAt = time(nullptr); }
    void setServiceName(const std::string& name) { serviceName = name; updatedAt = time(nullptr); }
    void setUsername(const std::string& user) { username = user; updatedAt = time(nullptr); }
    void setPassword(const std::string& newPassword) { password = newPassword; updatedAt = time(nullptr); }
    void setCategoryIndex(size_t index) { categoryIndex = index; updatedAt = time(nullptr); }
    void setNotes(const std::string& newNotes) { notes = newNotes; updatedAt = time(nullptr); }
    void setNotes2(const std::string& newNotes2) { notes2 = newNotes2; updatedAt = time(nullptr); }
    void setNotes3(const std::string& newNotes3) { notes3 = newNotes3; updatedAt = time(nullptr); }
    void setLink(const std::string& newLink) { link = newLink; updatedAt = time(nullptr); }
    void setExpiresAt(time_t expiry) { expiresAt = expiry; updatedAt = time(nullptr); }
    void setCreatedAt(time_t created) { createdAt = created; }
    void setUpdatedAt(time_t updated) { updatedAt = updated; }

    // Methodes utils
    bool empty() const { 
        return serviceName.empty() && username.empty() && password.empty() && 
               notes.empty() && notes2.empty() && notes3.empty() && link.empty();
    }
};

#endif // ENTRY_H
