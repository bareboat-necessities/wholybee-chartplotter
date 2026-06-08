#pragma once
#include <QString>

// Centralised palette so the side menu, the hamburger button, and the chart's
// floating zoom buttons all draw in matching colours regardless of the system
// theme. The OS theme is sampled once at startup (Qt 6.5+ colour-scheme API);
// switching the OS theme while the app is running requires a restart.
namespace theme {

bool isDark();

// Side-menu palette (the slide-out drawer plus its action items).
struct MenuPalette {
    QString panelBg;        // drawer surface
    QString panelBorder;    // right-edge border
    QString titleBg;        // top title bar
    QString titleFg;
    QString headerBg;       // section header strip
    QString headerFg;
    QString actionBg;       // an item's normal background (== panelBg)
    QString actionFg;
    QString actionPressed;  // pressed-state background
    QString accent;         // active chart set / checked items
    QString separator;      // divider line within a section
    QString hint;           // dim explanatory label
};
const MenuPalette& menu();

// Floating circular/rounded buttons over the chart (hamburger + zoom +/-).
// Translucent so they sit nicely over a chart of any colour.
struct OverlayBtnPalette {
    QString bg;
    QString border;
    QString fg;
    QString pressed;
};
const OverlayBtnPalette& overlayBtn();

// Numeric input "card" used inside dialogs: a text field flanked by big +/-
// buttons (TouchSpinBox). The field and button get subtly different shades in
// light mode so the buttons read as buttons; in dark mode they match the
// dialog body so the field doesn't burn out as a white block.
struct InputPalette {
    QString fieldBg;
    QString buttonBg;
    QString border;
    QString fg;
    QString pressed;
};
const InputPalette& input();

// Dimmed text colour for hint/secondary labels inside dialogs. Chosen so it
// reads as "muted" against either a light or dark dialog background instead
// of being unreadable hard-coded dark grey under dark mode.
QString textMuted();

} // namespace theme
