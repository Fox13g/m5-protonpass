#ifndef I_INPUT_H
#define I_INPUT_H

#include "InputKeys.h"

class IInput {
public:
    virtual ~IInput() = default;
    virtual char handler() = 0;
    virtual void waitPress() = 0;
};

#endif // I_INPUT_H
