// SPDX-License-Identifier: GPL-3.0-or-later
#include "snow_shot/presentation/rackhoverpolicy.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace snow_shot::presentation;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

// A 1512x982 display whose 30 px menu bar is excluded from the available area.
const QRect Screen(0, 0, 1512, 982);
const QRect Available(0, 30, 1512, 952);

void parsesDisplayModes() {
    require(rackDisplayModeFromName(u"hover") == RackDisplayMode::Hover, "hover is parsed");
    require(rackDisplayModeFromName(u"hotkey") == RackDisplayMode::Hotkey, "hotkey is parsed");
    require(rackDisplayModeFromName(u"always") == RackDisplayMode::Always, "always is parsed");
    require(rackDisplayModeFromName(u"unexpected") == RackDisplayMode::Hover,
            "unknown modes fall back to hover");
}

void placesRackBelowMenuBar() {
    const QRect collapsed = rackTargetGeometry(Screen, Available, false);
    const QRect expanded = rackTargetGeometry(Screen, Available, true);
    require(collapsed.top() == Available.top() && expanded.top() == Available.top(),
            "the rack starts below the menu bar instead of under it");
    require(collapsed.size() == QSize(112, 8), "the collapsed handle is a thin capsule");
    require(expanded.size() == QSize(660, 260), "the expanded panel has a fixed size");
    require(Screen.contains(expanded) && expanded.left() + expanded.width() + 12 == Screen.right(),
            "the panel stays on screen with a right margin");
    require(rackTargetGeometry(QRect(0, 0, 500, 400), QRect(0, 0, 500, 400), true).width() == 476,
            "narrow screens shrink the panel width");
}

void detectsHotZone() {
    const int right = Screen.right();
    require(rackInHotZone(Screen, Available, {right - 100, 0}), "the top edge is hot");
    require(rackInHotZone(Screen, Available, {right - 100, Available.top() + 8}),
            "the band just below the menu bar is hot");
    require(!rackInHotZone(Screen, Available, {right - 100, Available.top() + 9}),
            "the area below the hot band is ignored");
    require(rackInHotZone(Screen, Available, {right - 180, 0}) &&
                rackInHotZone(Screen, Available, {right - 20, 0}),
            "hot zone bounds are inclusive");
    require(!rackInHotZone(Screen, Available, {right - 181, 0}) &&
                !rackInHotZone(Screen, Available, {right - 19, 0}),
            "pointers outside the horizontal range are ignored");
}

void hoverAreaIncludesMenuBarBand() {
    const QRect panel = rackTargetGeometry(Screen, Available, true);
    require(rackInHoverArea(panel, Screen, {panel.center().x(), 0}),
            "resting on the top edge above the panel keeps it hovered");
    require(rackInHoverArea(panel, Screen, panel.center()), "the panel itself is hovered");
    require(!rackInHoverArea(panel, Screen, {panel.left() - 1, 0}),
            "the menu bar beside the panel is not hovered");
    require(!rackInHoverArea(panel, Screen, {panel.center().x(), panel.bottom() + 1}),
            "leaving below the panel is not hovered");
}

void interactionsBlockCollapse() {
    const RackInteractionState idle;
    require(!rackCollapseBlocked(idle, RackCollapseTrigger::Automatic) &&
                !rackCollapseBlocked(idle, RackCollapseTrigger::Explicit),
            "an idle rack can always collapse");
    const RackInteractionState search{.searchFocused = true};
    require(rackCollapseBlocked(search, RackCollapseTrigger::Automatic),
            "a focused search keeps the rack open when the pointer leaves");
    require(!rackCollapseBlocked(search, RackCollapseTrigger::Explicit),
            "a hotkey, Escape or close button dismisses a focused search");
    for (const RackInteractionState state :
         {RackInteractionState{.contentInteraction = true}, RackInteractionState{.popupOpen = true},
          RackInteractionState{.modalOpen = true}}) {
        require(rackCollapseBlocked(state, RackCollapseTrigger::Automatic) &&
                    rackCollapseBlocked(state, RackCollapseTrigger::Explicit),
                "drags, menus and dialogs keep the rack open for every trigger");
    }
}

void decidesPointerActions() {
    RackPointerState state{.inHotZone = true};
    require(rackPointerAction(state) == RackPointerAction::Expand, "hover mode expands on entry");
    state.mode = RackDisplayMode::Hotkey;
    require(rackPointerAction(state) == RackPointerAction::None, "hotkey mode ignores hovering");
    state.mode = RackDisplayMode::Always;
    state.expanded = true;
    require(rackPointerAction(state) == RackPointerAction::None,
            "always mode never schedules a collapse");

    state = {.expanded = true};
    require(rackPointerAction(state) == RackPointerAction::ScheduleCollapse,
            "leaving the panel schedules a delayed collapse");
    state.collapsePending = true;
    require(rackPointerAction(state) == RackPointerAction::None,
            "a pending collapse is not restarted on every poll");
    state.inHoverArea = true;
    require(rackPointerAction(state) == RackPointerAction::CancelCollapse,
            "returning before the delay cancels the collapse");
    state = {.expanded = true, .interacting = true, .collapsePending = true};
    require(rackPointerAction(state) == RackPointerAction::CancelCollapse,
            "an interaction cancels a pending collapse");
    state = {.expanded = true, .inHoverArea = true};
    require(rackPointerAction(state) == RackPointerAction::None,
            "hovering an open panel needs no action");

    require(rackCollapseAfterDelay(false, false), "the delay collapses an abandoned rack");
    require(!rackCollapseAfterDelay(true, false) && !rackCollapseAfterDelay(false, true),
            "the delay re-checks hover and interaction before collapsing");
}

// Replays the window's 50 ms pointer poll against a scripted cursor path.
struct Simulation {
    bool expanded = false;
    int collapseDueMs = -1;
    int collapsedAtMs = -1;

    void poll(int nowMs, const QPoint& cursor) {
        if (collapseDueMs >= 0 && nowMs >= collapseDueMs) {
            collapseDueMs = -1;
            const QRect window = rackTargetGeometry(Screen, Available, expanded);
            if (rackCollapseAfterDelay(rackInHoverArea(window, Screen, cursor), false)) {
                expanded = false;
                collapsedAtMs = nowMs;
            }
        }
        const QRect window = rackTargetGeometry(Screen, Available, expanded);
        const RackPointerState state{
            .expanded = expanded,
            .inHotZone = !expanded && rackInHotZone(Screen, Available, cursor),
            .inHoverArea = expanded && rackInHoverArea(window, Screen, cursor),
            .collapsePending = collapseDueMs >= 0};
        switch (rackPointerAction(state)) {
        case RackPointerAction::Expand:
            expanded = true;
            break;
        case RackPointerAction::CancelCollapse:
            collapseDueMs = -1;
            break;
        case RackPointerAction::ScheduleCollapse:
            collapseDueMs = nowMs + RackCollapseDelayMs;
            break;
        case RackPointerAction::None:
            break;
        }
    }
};

void decidesCloseDisposition() {
    require(rackCloseDisposition(RackDisplayMode::Always) == RackCloseDisposition::RestoreAlways,
            "always mode restores the panel after an accepted close");
    require(rackCloseDisposition(RackDisplayMode::Hover) == RackCloseDisposition::Dismiss,
            "hover mode dismisses on close");
    require(rackCloseDisposition(RackDisplayMode::Hotkey) == RackCloseDisposition::Dismiss,
            "hotkey mode dismisses on close");
}

void simulatesHoverSession() {
    Simulation rack;
    const QPoint topEdge(Screen.right() - 100, 0);
    int now = 0;
    rack.poll(now, topEdge);
    require(rack.expanded, "touching the top edge expands the rack");
    for (now = RackPointerPollMs; now <= 5000; now += RackPointerPollMs)
        rack.poll(now, topEdge);
    require(rack.expanded && rack.collapseDueMs < 0,
            "resting on the top edge for five seconds never schedules a collapse");

    const int leftAtMs = now;
    const QPoint away(Screen.center());
    for (; rack.expanded && now <= leftAtMs + 1000; now += RackPointerPollMs)
        rack.poll(now, away);
    require(!rack.expanded, "leaving the rack collapses it");
    const int closedAfterMs = rack.collapsedAtMs - leftAtMs + RackAnimationMs;
    require(rack.collapsedAtMs - leftAtMs >= RackCollapseDelayMs,
            "the collapse waits for the debounce delay");
    require(closedAfterMs <= RackCollapseDelayMs + RackPointerPollMs + RackAnimationMs,
            "the rack finishes closing within one poll of delay plus animation");

    Simulation bounce;
    bounce.poll(0, topEdge);
    bounce.poll(50, away);
    bounce.poll(100, rackTargetGeometry(Screen, Available, true).center());
    for (now = 150; now <= 600; now += RackPointerPollMs)
        bounce.poll(now, rackTargetGeometry(Screen, Available, true).center());
    require(bounce.expanded && bounce.collapsedAtMs < 0,
            "briefly leaving and returning within the delay keeps the rack open");
}
} // namespace

int main() {
    parsesDisplayModes();
    placesRackBelowMenuBar();
    detectsHotZone();
    hoverAreaIncludesMenuBarBand();
    interactionsBlockCollapse();
    decidesPointerActions();
    decidesCloseDisposition();
    simulatesHoverSession();
    return 0;
}
