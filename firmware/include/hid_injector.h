#pragma once

#include <stdint.h>
#include <stddef.h>
#include "types.h"

namespace sentinel {

/**
 * Representa um relatório de tecla HID física (Modifier + Keycode USB HID Usage Table 0x07),
 * com suporte opcional a Dead Keys (ex: '~', '^', '`' no layout PT-BR ABNT2).
 */
struct HidKeyStroke {
    uint8_t deadKeyModifier; // Modificador da tecla morta prévia (0 se não houver)
    uint8_t deadKeyCode;     // Keycode da tecla morta prévia (0 se não houver, ex: SPACE após dead key)
    uint8_t modifier;        // Bitmask: 0x02 = Left Shift, 0x40 = Right Alt (AltGr)
    uint8_t keycode;         // USB HID Usage ID (ex: 0x04 = 'a')
};

class HidInjector {
public:
    HidInjector();

    /**
     * Inicializa o periférico USB OTG nativo (`TinyUSB HID Keyboard`) e prepara o subsistema BLE HID.
     */
    bool begin();

    void setLayout(KeyboardLayout layout) { m_layout = layout; }
    KeyboardLayout layout() const { return m_layout; }
    void toggleLayout();

    void setTransportMode(HidTransportMode mode);
    HidTransportMode transportMode() const { return m_mode; }
    void toggleTransportMode();

    /**
     * Traduz um caractere ASCII para o `HidKeyStroke` exato segundo o layout ativo
     * (`PT_BR_ABNT2` ou `US_ANSI`). Método estático puro testável em ambiente nativo!
     */
    static bool mapCharToHid(char ch, KeyboardLayout layout, HidKeyStroke& outStroke);

    /**
     * Injeta uma string aplicando o mapeamento de layout e o delay anti-perda de teclas (12 ms).
     */
    bool typeString(const char* text);

    /**
     * Envia teclas de controlo dedicadas (`TAB` ou `ENTER`).
     */
    void sendTab();
    void sendEnter();

    /**
     * Executa uma sequência completa de injeção para o item decifrado e limpa os buffers.
     */
    bool executeInjectionSequence(
        InjectSequenceType seqType,
        const char* username,
        const char* password,
        const char* totpCode
    );

private:
    void sendRawKeyStroke(uint8_t modifier, uint8_t keycode);

    KeyboardLayout   m_layout;
    HidTransportMode m_mode;
    bool             m_usbInitialized;
    bool             m_bleInitialized;
};

} // namespace sentinel

