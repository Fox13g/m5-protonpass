#ifndef ENTRY_SERVICE_H
#define ENTRY_SERVICE_H

#include <string>
#include <vector>
#include <ctime>
#include <random>
#include <Repositories/EntryRepository.h>
#include <Models/Entry.h>
#include <Models/Field.h>

class EntryService {
private:
    EntryRepository& repository;

public:
    EntryService(EntryRepository& repo) : repository(repo) {}

    bool isEntryExpired(const Entry& entry);

    void addEntry(const Entry& entry);
    void deleteEntry(const Entry& entry);
    bool updateEntry(const Entry& oldEntry, const Entry& newEntry);
    Entry findEntryById(const std::string& id);
    std::vector<Entry> getAllEntries();
    void setEntries(std::vector<Entry>& ents);
    void setContainerName(std::string& name);
    std::string getVaultName();
    Entry getEmptyEntry();
    std::vector<Entry> getEmptyEntries(size_t count);
    bool updateField(Entry& entry, const Field& field);
};

#endif // ENTRY_SERVICE_H
