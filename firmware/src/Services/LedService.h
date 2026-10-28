#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <FastLED.h>
#include <States/GlobalState.h>

#define LED_PIN 21 // Builtin, TODO: use context

class LedService {
public:
    void blink();
    void showLed();
    void clearLed();

private:
    GlobalState& globalState = GlobalState::getInstance();
    CRGB leds[1];
};


#endif
