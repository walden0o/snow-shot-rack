// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SNOW_SHOT_PRESENTATION_RACKHOVERPOLICY_H
#define SNOW_SHOT_PRESENTATION_RACKHOVERPOLICY_H

#include <QPoint>
#include <QRect>
#include <QStringView>

// Window-free decisions behind the rack's expand/collapse behavior.
namespace snow_shot::presentation {
inline constexpr int RackCollapseDelayMs = 200;
inline constexpr int RackAnimationMs = 170;
inline constexpr int RackPointerPollMs = 50;

enum class RackDisplayMode { Hover, Hotkey, Always };
// Unknown names fall back to hover, matching the configuration default.
[[nodiscard]] RackDisplayMode rackDisplayModeFromName(QStringView name);

// `screen` is the full screen geometry; `available` excludes the menu bar.
[[nodiscard]] QRect rackTargetGeometry(const QRect& screen, const QRect& available, bool expanded);
[[nodiscard]] bool rackInHotZone(const QRect& screen, const QRect& available, const QPoint& cursor);
// The band above the window up to the screen edge counts as inside the rack.
[[nodiscard]] bool rackInHoverArea(const QRect& window, const QRect& screen, const QPoint& cursor);

struct RackInteractionState {
    bool contentInteraction = false;
    bool popupOpen = false;
    bool modalOpen = false;
    bool searchFocused = false;
};
enum class RackCollapseTrigger { Automatic, Explicit };
// Explicit dismissal overrides search focus, but never an active drag, menu or dialog.
[[nodiscard]] bool rackCollapseBlocked(const RackInteractionState& state,
                                       RackCollapseTrigger trigger);

struct RackPointerState {
    RackDisplayMode mode = RackDisplayMode::Hover;
    bool expanded = false;
    bool inHotZone = false;
    bool inHoverArea = false;
    bool interacting = false;
    bool collapsePending = false;
};
enum class RackPointerAction { None, Expand, CancelCollapse, ScheduleCollapse };
[[nodiscard]] RackPointerAction rackPointerAction(const RackPointerState& state);
// Re-checked when the collapse delay elapses, since the pointer may have returned.
[[nodiscard]] bool rackCollapseAfterDelay(bool inHoverArea, bool interacting);
} // namespace snow_shot::presentation
#endif
