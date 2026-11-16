// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackhoverpolicy.h"

#include <algorithm>

namespace snow_shot::presentation {
RackDisplayMode rackDisplayModeFromName(QStringView name) {
    if (name == u"hotkey")
        return RackDisplayMode::Hotkey;
    if (name == u"always")
        return RackDisplayMode::Always;
    return RackDisplayMode::Hover;
}

QRect rackTargetGeometry(const QRect& screen, const QRect& available, bool expanded) {
    // Keep the rack below the macOS menu bar so the collapsed handle stays visible.
    const int top = available.top();
    const int width = std::min(660, screen.width() - 24);
    return expanded ? QRect(screen.right() - width - 12, top, width, 260)
                    : QRect(screen.right() - 156, top, 112, 8);
}

bool rackInHotZone(const QRect& screen, const QRect& available, const QPoint& cursor) {
    return cursor.y() >= screen.top() && cursor.y() <= available.top() + 8 &&
           cursor.x() >= screen.right() - 180 && cursor.x() <= screen.right() - 20;
}

bool rackInHoverArea(const QRect& window, const QRect& screen, const QPoint& cursor) {
    QRect area = window;
    area.setTop(std::min(window.top(), screen.top()));
    return area.contains(cursor);
}

bool rackCollapseBlocked(const RackInteractionState& state, RackCollapseTrigger trigger) {
    if (state.contentInteraction || state.popupOpen || state.modalOpen)
        return true;
    return trigger == RackCollapseTrigger::Automatic && state.searchFocused;
}

RackPointerAction rackPointerAction(const RackPointerState& state) {
    if (state.mode != RackDisplayMode::Hover)
        return RackPointerAction::None;
    if (!state.expanded)
        return state.inHotZone ? RackPointerAction::Expand : RackPointerAction::None;
    if (state.inHoverArea || state.interacting)
        return state.collapsePending ? RackPointerAction::CancelCollapse : RackPointerAction::None;
    return state.collapsePending ? RackPointerAction::None : RackPointerAction::ScheduleCollapse;
}

bool rackCollapseAfterDelay(bool inHoverArea, bool interacting) {
    return !inHoverArea && !interacting;
}
} // namespace snow_shot::presentation
