#ifndef CARDPUTER_INPUT_H
#define CARDPUTER_INPUT_H

#include <map>
#include <M5Cardputer.h>
#include "IInput.h"

namespace inputs {

class CardputerInput : public IInput {
public:
    char handler() override;
    void waitPress() override;
};

}

#endif // CARDPUTER_INPUT_H
