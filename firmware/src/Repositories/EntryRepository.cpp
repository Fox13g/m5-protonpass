#include "EntryRepository.h"

void EntryRepository::addEntry(const Entry& entry) {
    entries.push_back(entry);
}

const std::vector<Entry>& EntryRepository::getAllEntries() const {
    return entries;
}

void EntryRepository::setEntries(std::vector<Entry>& ents) {
    entries = ents;
}

const Entry& EntryRepository::findEntryById(const std::string& id) const {
    static const Entry emptyEntry;
    for (const auto& entry : entries) {
        if (entry.getId() == id) {
            return entry;
        }
    }
    return emptyEntry;
}

bool EntryRepository::deleteEntry(const Entry& entry) {
    auto it = std::remove_if(entries.begin(), entries.end(),
                             [&entry](const Entry& currentEntry) {
                                 return currentEntry.getServiceName() == entry.getServiceName() &&
                                        currentEntry.getUsername() == entry.getUsername();
                             });
    if (it != entries.end()) {
        entries.erase(it, entries.end());
        return true;
    }
    return false;
}

bool EntryRepository::updateEntry(const Entry& oldEntry, const Entry& newEntry) {
    for (auto& entry : entries) {
        if (entry.getServiceName() == oldEntry.getServiceName() &&
            entry.getUsername() == oldEntry.getUsername() &&
            entry.getPassword() == oldEntry.getPassword() &&
            entry.getNotes() == oldEntry.getNotes()) {
            entry = newEntry;
            return true;
        }
    }
    return false;
}


std::string EntryRepository::getContainerName() {
    return containerName;
}

void EntryRepository::setContainerName(std::string& name) {
    containerName = name;
}
