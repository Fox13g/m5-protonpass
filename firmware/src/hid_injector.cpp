#include "hid_injector.h"
#include "config.h"
#include <string.h>

#if !defined(UNIT_TEST_NATIVE)
#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

namespace {
USBHIDKeyboard g_usbKeyboard;
}
#endif

namespace sentinel {

// ============================================================================
// Constantes USB HID Usage Table (Page 0x07) & Modificadores
// ============================================================================
static constexpr uint8_t MOD_NONE   = 0x00;
static constexpr uint8_t MOD_LSHIFT = 0x02;
static constexpr uint8_t MOD_RALT   = 0x40; // AltGr no layout PT-BR ABNT2

static constexpr uint8_t HID_KEY_ENTER     = 0x28;
static constexpr uint8_t HID_KEY_TAB       = 0x2B;
static constexpr uint8_t HID_KEY_SPACE     = 0x2C;
static constexpr uint8_t HID_KEY_MINUS     = 0x2D; // '-' e '_'
static constexpr uint8_t HID_KEY_EQUAL     = 0x2E; // '=' e '+'
static constexpr uint8_t HID_KEY_LBRACKET  = 0x2F; // US: '[' / ABNT2: Dead key '´' e '`'
static constexpr uint8_t HID_KEY_RBRACKET  = 0x30; // US: ']' / ABNT2: '[' e '{'
static constexpr uint8_t HID_KEY_BACKSLASH = 0x31; // US: '\' / ABNT2: ']' e '}'
static constexpr uint8_t HID_KEY_SEMICOLON = 0x33; // US: ';' / ABNT2: 'ç'
static constexpr uint8_t HID_KEY_QUOTE     = 0x34; // US: ''' / ABNT2: Dead key '~' e '^'
static constexpr uint8_t HID_KEY_GRAVE     = 0x35; // US: '`' / ABNT2: ''' e '"'
static constexpr uint8_t HID_KEY_COMMA     = 0x36; // ',' e '<'
static constexpr uint8_t HID_KEY_PERIOD    = 0x37; // '.' e '>'
static constexpr uint8_t HID_KEY_SLASH     = 0x38; // US: '/' e '?' / ABNT2: ';' e ':'
static constexpr uint8_t HID_KEY_NON_US_BS = 0x64; // ABNT2: '\' e '|' (ao lado do Left Shift)
static constexpr uint8_t HID_KEY_INTL1     = 0x87; // ABNT2: '/' e '?' (ao lado do Right Shift)

HidInjector::HidInjector()
    : m_layout(KeyboardLayout::PT_BR_ABNT2)
    , m_mode(HidTransportMode::USB_HID)
    , m_usbInitialized(false)
    , m_bleInitialized(false)
{}

bool HidInjector::begin() {
#if !defined(UNIT_TEST_NATIVE)
    if (!m_usbInitialized) {
        USB.begin();
        g_usbKeyboard.begin();
        m_usbInitialized = true;
    }
#endif
    return true;
}

void HidInjector::toggleLayout() {
    m_layout = (m_layout == KeyboardLayout::PT_BR_ABNT2)
        ? KeyboardLayout::US_ANSI
        : KeyboardLayout::PT_BR_ABNT2;
}

void HidInjector::setTransportMode(HidTransportMode mode) {
    m_mode = mode;
}

void HidInjector::toggleTransportMode() {
    m_mode = (m_mode == HidTransportMode::USB_HID)
        ? HidTransportMode::BLE_HID
        : HidTransportMode::USB_HID;
}

static bool mapCharUsAnsi(char ch, HidKeyStroke& out) {
    memset(&out, 0, sizeof(out));

    if (ch >= 'a' && ch <= 'z') {
        out.keycode = static_cast<uint8_t>(0x04 + (ch - 'a'));
        return true;
    }
    if (ch >= 'A' && ch <= 'Z') {
        out.modifier = MOD_LSHIFT;
        out.keycode  = static_cast<uint8_t>(0x04 + (ch - 'A'));
        return true;
    }
    if (ch >= '1' && ch <= '9') {
        out.keycode = static_cast<uint8_t>(0x1E + (ch - '1'));
        return true;
    }
    if (ch == '0') {
        out.keycode = 0x27;
        return true;
    }

    switch (ch) {
        case ' ':  out.keycode = HID_KEY_SPACE; return true;
        case '!':  out.modifier = MOD_LSHIFT; out.keycode = 0x1E; return true;
        case '@':  out.modifier = MOD_LSHIFT; out.keycode = 0x1F; return true;
        case '#':  out.modifier = MOD_LSHIFT; out.keycode = 0x20; return true;
        case '$':  out.modifier = MOD_LSHIFT; out.keycode = 0x21; return true;
        case '%':  out.modifier = MOD_LSHIFT; out.keycode = 0x22; return true;
        case '^':  out.modifier = MOD_LSHIFT; out.keycode = 0x23; return true;
        case '&':  out.modifier = MOD_LSHIFT; out.keycode = 0x24; return true;
        case '*':  out.modifier = MOD_LSHIFT; out.keycode = 0x25; return true;
        case '(':  out.modifier = MOD_LSHIFT; out.keycode = 0x26; return true;
        case ')':  out.modifier = MOD_LSHIFT; out.keycode = 0x27; return true;
        case '-':  out.keycode = HID_KEY_MINUS; return true;
        case '_':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_MINUS; return true;
        case '=':  out.keycode = HID_KEY_EQUAL; return true;
        case '+':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_EQUAL; return true;
        case '[':  out.keycode = HID_KEY_LBRACKET; return true;
        case '{':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_LBRACKET; return true;
        case ']':  out.keycode = HID_KEY_RBRACKET; return true;
        case '}':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_RBRACKET; return true;
        case '\\': out.keycode = HID_KEY_BACKSLASH; return true;
        case '|':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_BACKSLASH; return true;
        case ';':  out.keycode = HID_KEY_SEMICOLON; return true;
        case ':':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_SEMICOLON; return true;
        case '\'': out.keycode = HID_KEY_QUOTE; return true;
        case '"':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_QUOTE; return true;
        case '`':  out.keycode = HID_KEY_GRAVE; return true;
        case '~':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_GRAVE; return true;
        case ',':  out.keycode = HID_KEY_COMMA; return true;
        case '<':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_COMMA; return true;
        case '.':  out.keycode = HID_KEY_PERIOD; return true;
        case '>':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_PERIOD; return true;
        case '/':  out.keycode = HID_KEY_SLASH; return true;
        case '?':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_SLASH; return true;
        default:   return false;
    }
}

static bool mapCharPtBrAbnt2(char ch, HidKeyStroke& out) {
    memset(&out, 0, sizeof(out));

    // Letras e dígitos seguem os mesmos keycodes base, mas os símbolos e acentos mudam no ABNT2!
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
        return mapCharUsAnsi(ch, out);
    }

    switch (ch) {
        case ' ':  out.keycode = HID_KEY_SPACE; return true;
        case '!':  out.modifier = MOD_LSHIFT; out.keycode = 0x1E; return true;
        case '@':  out.modifier = MOD_LSHIFT; out.keycode = 0x1F; return true;
        case '#':  out.modifier = MOD_LSHIFT; out.keycode = 0x20; return true;
        case '$':  out.modifier = MOD_LSHIFT; out.keycode = 0x21; return true;
        case '%':  out.modifier = MOD_LSHIFT; out.keycode = 0x22; return true;
        case '&':  out.modifier = MOD_LSHIFT; out.keycode = 0x24; return true;
        case '*':  out.modifier = MOD_LSHIFT; out.keycode = 0x25; return true;
        case '(':  out.modifier = MOD_LSHIFT; out.keycode = 0x26; return true;
        case ')':  out.modifier = MOD_LSHIFT; out.keycode = 0x27; return true;
        case '-':  out.keycode = HID_KEY_MINUS; return true;
        case '_':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_MINUS; return true;
        case '=':  out.keycode = HID_KEY_EQUAL; return true;
        case '+':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_EQUAL; return true;

        // No ABNT2, ''' e '"' ficam na tecla superior esquerda (HID_KEY_GRAVE = 0x35)
        case '\'': out.keycode = HID_KEY_GRAVE; return true;
        case '"':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_GRAVE; return true;

        // No ABNT2, '['/'{' ficam em 0x30 e ']'/'}' ficam em 0x31
        case '[':  out.keycode = HID_KEY_RBRACKET; return true;
        case '{':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_RBRACKET; return true;
        case ']':  out.keycode = HID_KEY_BACKSLASH; return true;
        case '}':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_BACKSLASH; return true;

        // No ABNT2, '\' e '|' ficam na tecla ISO 0x64 (ao lado do Left Shift)
        case '\\': out.keycode = HID_KEY_NON_US_BS; return true;
        case '|':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_NON_US_BS; return true;

        // No ABNT2, ';' e ':' ficam em 0x38 (HID_KEY_SLASH do US)
        case ';':  out.keycode = HID_KEY_SLASH; return true;
        case ':':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_SLASH; return true;

        // No ABNT2, '/' e '?' ficam em 0x87 (International1) ou AltGr+Q / AltGr+W
        case '/':  out.keycode = HID_KEY_INTL1; return true;
        case '?':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_INTL1; return true;

        case ',':  out.keycode = HID_KEY_COMMA; return true;
        case '<':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_COMMA; return true;
        case '.':  out.keycode = HID_KEY_PERIOD; return true;
        case '>':  out.modifier = MOD_LSHIFT; out.keycode = HID_KEY_PERIOD; return true;

        // Dead Keys no ABNT2 ('~', '^', '`'): envia a tecla morta seguida de ESPAÇO
        case '~':
            out.deadKeyModifier = MOD_NONE;
            out.deadKeyCode     = HID_KEY_QUOTE;
            out.modifier        = MOD_NONE;
            out.keycode         = HID_KEY_SPACE;
            return true;
        case '^':
            out.deadKeyModifier = MOD_LSHIFT;
            out.deadKeyCode     = HID_KEY_QUOTE;
            out.modifier        = MOD_NONE;
            out.keycode         = HID_KEY_SPACE;
            return true;
        case '`':
            out.deadKeyModifier = MOD_LSHIFT;
            out.deadKeyCode     = HID_KEY_LBRACKET;
            out.modifier        = MOD_NONE;
            out.keycode         = HID_KEY_SPACE;
            return true;
        default:
            return false;
    }
}

bool HidInjector::mapCharToHid(char ch, KeyboardLayout layout, HidKeyStroke& outStroke) {
    if (layout == KeyboardLayout::PT_BR_ABNT2) {
        return mapCharPtBrAbnt2(ch, outStroke);
    }
    return mapCharUsAnsi(ch, outStroke);
}

void HidInjector::sendRawKeyStroke(uint8_t modifier, uint8_t keycode) {
#if !defined(UNIT_TEST_NATIVE)
    KeyReport report;
    memset(&report, 0, sizeof(report));
    report.modifiers = modifier;
    report.keys[0]   = keycode;
    g_usbKeyboard.sendReport(&report);
    delay(HID_INTER_KEY_DELAY_MS);

    memset(&report, 0, sizeof(report));
    g_usbKeyboard.sendReport(&report);
    delay(HID_INTER_KEY_DELAY_MS);
#else
    (void)modifier;
    (void)keycode;
#endif
}

bool HidInjector::typeString(const char* text) {
    if (!text) return false;

    for (const char* p = text; *p != '\0'; ++p) {
        HidKeyStroke stroke;
        if (!mapCharToHid(*p, m_layout, stroke)) {
            continue;
        }
        if (stroke.deadKeyCode != 0) {
            sendRawKeyStroke(stroke.deadKeyModifier, stroke.deadKeyCode);
        }
        sendRawKeyStroke(stroke.modifier, stroke.keycode);
    }
    return true;
}

void HidInjector::sendTab() {
    sendRawKeyStroke(MOD_NONE, HID_KEY_TAB);
}

void HidInjector::sendEnter() {
    sendRawKeyStroke(MOD_NONE, HID_KEY_ENTER);
}

bool HidInjector::executeInjectionSequence(
    InjectSequenceType seqType,
    const char* username,
    const char* password,
    const char* totpCode
) {
    switch (seqType) {
        case InjectSequenceType::FULL_AUTO_TYPE:
            if (username && strlen(username) > 0) {
                typeString(username);
                sendTab();
#if !defined(UNIT_TEST_NATIVE)
                delay(HID_TAB_STABILIZE_MS);
#endif
            }
            if (password && strlen(password) > 0) {
                typeString(password);
                sendEnter();
            }
            return true;

        case InjectSequenceType::USERNAME_ONLY:
            if (username && strlen(username) > 0) {
                return typeString(username);
            }
            return false;

        case InjectSequenceType::PASSWORD_ONLY:
            if (password && strlen(password) > 0) {
                return typeString(password);
            }
            return false;

        case InjectSequenceType::TOTP_ONLY:
            if (totpCode && strlen(totpCode) > 0) {
                return typeString(totpCode);
            }
            return false;
    }
    return false;
}

} // namespace sentinel

