#ifndef PASSWORD_REPOSITORY_H
#define PASSWORD_REPOSITORY_H

#include <vector>
#include <string>
#include <algorithm>
#include <Models/Entry.h>

class EntryRepository {
private:
    std::vector<Entry> entries;
    std::string containerName;

public:
    void addEntry(const Entry& entry);
    const std::vector<Entry>& getAllEntries() const;
    void setEntries(std::vector<Entry>& ents);
    const Entry& findEntryById(const std::string& id) const;
    bool deleteEntry(const Entry& entry);
    bool updateEntry(const Entry& oldEntry, const Entry& newEntry);

    void setContainerName(std::string& name);
    std::string getContainerName();
};

#endif // PASSWORD_REPOSITORY_H
