#include "theme.hpp"
#include <QGuiApplication>
#include <QStyleHints>

namespace theme {

bool isDark() {
    if (auto* h = QGuiApplication::styleHints())
        return h->colorScheme() == Qt::ColorScheme::Dark;
    return false;
}

namespace {
// Light palette is the pre-existing design (kept identical to what shipped
// before this dark-theme work).
const MenuPalette kLightMenu = {
    QStringLiteral("#fbfbfb"),       // panelBg
    QStringLiteral("#c8c8c8"),       // panelBorder
    QStringLiteral("#1a3a5c"),       // titleBg (brand navy)
    QStringLiteral("white"),         // titleFg
    QStringLiteral("#eef1f4"),       // headerBg
    QStringLiteral("#5a5a5a"),       // headerFg
    QStringLiteral("#fbfbfb"),       // actionBg
    QStringLiteral("#1a1a1a"),       // actionFg
    QStringLiteral("#dce6f0"),       // actionPressed
    QStringLiteral("#12407a"),       // accent
    QStringLiteral("#d4d9de"),       // separator
    QStringLiteral("#888"),          // hint
};

// Dark palette: near-black surfaces with light text. The brand navy is kept
// for the title bar (still readable as a strong accent over the dark body),
// the checked-item accent is lightened so it's visible on dark, and the
// pressed-state colour gets a small blue tint so it still feels "pressable".
const MenuPalette kDarkMenu = {
    QStringLiteral("#1e1e1e"),       // panelBg
    QStringLiteral("#404040"),       // panelBorder
    QStringLiteral("#1a3a5c"),       // titleBg
    QStringLiteral("white"),         // titleFg
    QStringLiteral("#2a2a2a"),       // headerBg
    QStringLiteral("#b0b0b0"),       // headerFg
    QStringLiteral("#1e1e1e"),       // actionBg
    QStringLiteral("#e6e6e6"),       // actionFg
    QStringLiteral("#2c3a4c"),       // actionPressed
    QStringLiteral("#6aa6ff"),       // accent (lighter blue for dark bg)
    QStringLiteral("#3a3a3a"),       // separator
    QStringLiteral("#888"),          // hint (neutral; works on either bg)
};

// Floating buttons: translucent white-ish or near-black so the chart still
// shows through. The text colour pins to readable values in each mode (the
// hamburger and zoom glyphs would otherwise inherit the system text colour
// and become invisible on the opposite-mode background).
const OverlayBtnPalette kLightOverlay = {
    QStringLiteral("rgba(255,255,255,0.92)"),
    QStringLiteral("#b0b0b0"),
    QStringLiteral("#1a3a5c"),
    QStringLiteral("#dce6f0"),
};
const OverlayBtnPalette kDarkOverlay = {
    QStringLiteral("rgba(30,30,30,0.92)"),
    QStringLiteral("#6a6a6a"),
    QStringLiteral("#d0d0d0"),
    QStringLiteral("#354a66"),
};

// Numeric input + button "card" for TouchSpinBox.
const InputPalette kLightInput = {
    QStringLiteral("white"),     // fieldBg
    QStringLiteral("#f4f6f8"),   // buttonBg (subtle contrast vs white field)
    QStringLiteral("#b0b0b0"),   // border
    QStringLiteral("#1a1a1a"),   // fg
    QStringLiteral("#dce6f0"),   // pressed
};
const InputPalette kDarkInput = {
    QStringLiteral("#2a2a2a"),   // fieldBg (a notch lighter than the dialog body)
    QStringLiteral("#303030"),   // buttonBg
    QStringLiteral("#5a5a5a"),   // border
    QStringLiteral("#e6e6e6"),   // fg
    QStringLiteral("#354a66"),   // pressed
};
} // namespace

const MenuPalette& menu()              { return isDark() ? kDarkMenu : kLightMenu; }
const OverlayBtnPalette& overlayBtn()  { return isDark() ? kDarkOverlay : kLightOverlay; }
const InputPalette& input()            { return isDark() ? kDarkInput : kLightInput; }
QString textMuted()                    { return isDark() ? QStringLiteral("#9a9a9a")
                                                         : QStringLiteral("#666"); }

} // namespace theme
