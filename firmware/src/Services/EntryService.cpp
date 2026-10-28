#include "EntryService.h"

bool EntryService::isEntryExpired(const Entry& entry) {
    if (entry.getExpiresAt() == 0) return false;
    return std::time(nullptr) > entry.getExpiresAt();
}

void EntryService::addEntry(const Entry& entry) {
    repository.addEntry(entry);
}

void EntryService::deleteEntry(const Entry& entry) {
    repository.deleteEntry(entry);
}

bool EntryService::updateEntry(const Entry& oldEntry, const Entry& newEntry) {
    return repository.updateEntry(oldEntry, newEntry);
}

Entry EntryService::findEntryById(const std::string& id) {
    auto entry = repository.findEntryById(id);
    return entry;
}

std::vector<Entry> EntryService::getAllEntries() {
    return repository.getAllEntries();
}

void EntryService::setEntries(std::vector<Entry>& ents) {
    repository.setEntries(ents);
}

std::string EntryService::getVaultName() {
    return repository.getContainerName();
}

void EntryService::setContainerName(std::string& name) {
    repository.setContainerName(name);
}

Entry EntryService::getEmptyEntry() {
    return Entry();
}

std::vector<Entry> EntryService::getEmptyEntries(size_t count) {
    std::vector<Entry> entries;
    entries.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        entries.push_back(Entry());
    }

    return entries;
}

bool EntryService::updateField(Entry& entry, const Field& field) {
    const std::string& fieldName = field.getLabel();
    const std::string& newValue = field.getValue();
    Entry originalEntry = entry;

    if (fieldName == "User") {
        entry.setUsername(newValue);
    } else if (fieldName == "Pass") {
        entry.setPassword(newValue);
    } else if (fieldName == "2FA") {
        entry.setNotes2(newValue);
    } else if (fieldName == "Note") {
        entry.setNotes(newValue);
    } else {
        return false;
    }

    return repository.updateEntry(originalEntry, entry);
}
