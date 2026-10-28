#ifndef INACTIVITY_MANAGER_H
#define INACTIVITY_MANAGER_H

#include "../Views/IView.h"
#include "../Services/EntryService.h"
#include "../States/GlobalState.h"
#include <chrono>
#include <Arduino.h>

class InactivityManager {
private:
    IView& display;
    std::chrono::steady_clock::time_point lastInteractionTime;

    bool isDimmed = false;
    bool isShutdown = false;
    bool isLocked = false;

    GlobalState& globalState = GlobalState::getInstance();

public:
    InactivityManager(IView& display) : display(display) {
        lastInteractionTime = std::chrono::steady_clock::now();
    }

    void reset() {
        lastInteractionTime = std::chrono::steady_clock::now();
        globalState.setVaultIsLocked(false);
        if (isDimmed || isShutdown) {
            restoreScreen();
        }
        isLocked = false;
    }

    void update() {
        auto now = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastInteractionTime).count();

        // **Réduction de la luminosité**
        if (!isDimmed && elapsedMs >= globalState.getInactivityBrightnessTimeout()) {
            dimScreen();
        }

        // **Extinction complète de l'écran**
        if (!isShutdown && elapsedMs >= globalState.getInactivityScreenTimeout()) {
            shutdownScreen();
        }

        // **Verrouillage du coffre**
        if (!isLocked && elapsedMs >= globalState.getInactivityLockTimeout()) {
            lockVault();
        }
    }

    void dimScreen() {
        uint16_t currentBrightness = display.getBrightness();
        while (currentBrightness > 20) {
            currentBrightness -= 1; // Diminue progressivement
            display.setBrightness(currentBrightness);
            delay(5);
        }
        isDimmed = true;
    }

    void shutdownScreen() {
        uint16_t currentBrightness = display.getBrightness();
        while (currentBrightness > 1) {
            currentBrightness -= 1; // Diminue progressivement
            display.setBrightness(currentBrightness);
            delay(5);
        }   
        isShutdown = true;
    }

    void restoreScreen() {
        display.setBrightness(globalState.getSelectedScreenBrightness());
        isDimmed = false;
        isShutdown = false;
    }

    void lockVault() {
        shutdownScreen();
        globalState.setVaultIsLocked(true);
        isShutdown = true;
        isLocked = true;
    }

    bool getVaultIsLocked() const {
        return isLocked;
    }

    void setVaultIsLocked(bool lockState) {
        isLocked = lockState;
    }
};

#endif // INACTIVITY_MANAGER_H
