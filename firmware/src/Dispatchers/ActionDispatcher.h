#ifndef APP_DISPATCHER_H
#define APP_DISPATCHER_H

#include <Providers/DependencyProvider.h>
#include <Models/Entry.h>
#include <Models/Field.h>
#include <Enums/ActionEnum.h>
#include <Enums/KeyboardLayoutEnum.h>
#include <Enums/IconEnum.h>
#include <Enums/ContextEnum.h>
#include <vector>
#include <string>

class ActionDispatcher {
public:
    explicit ActionDispatcher(DependencyProvider& provider);

    void setup();
    void run();

private:
    DependencyProvider& provider;
    ContextEnum context; // Selection Context
    Entry selectedEntry;
    Field selectedField;

    // Méthodes privées
    ActionEnum determineActionByContext();
    void executeAction(ActionEnum action);

    GlobalState& globalState = GlobalState::getInstance();
};

#endif // APP_DISPATCHER_H
