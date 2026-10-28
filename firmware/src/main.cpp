#ifndef UNIT_TEST

#include <M5Cardputer.h>
#include <Views/CardputerView.h>
#include <Inputs/CardputerInput.h>
#include <Providers/DependencyProvider.h>
#include <Dispatchers/ActionDispatcher.h>

using namespace views;
using namespace inputs;

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    CardputerView display;
    CardputerInput input;

    DependencyProvider provider(display, input);
    provider.setup();

    ActionDispatcher dispatcher(provider);
    dispatcher.setup();
    dispatcher.run();
}

void loop() {
    // Empty as all logic is handled in dispatcher
}

#endif
