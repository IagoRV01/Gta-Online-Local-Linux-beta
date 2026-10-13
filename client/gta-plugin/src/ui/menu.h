// Small list menu drawn with natives (Online interaction-menu look), driven by GTA control actions
// so keyboard and gamepad both work: up/down select, left/right change, accept, cancel = back.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ui {

struct Item {
    std::string label;
    std::function<std::string()> value;      // right-hand text; empty function = none
    std::function<void(int dir)> change;     // left (-1) / right (+1); hold to repeat
    std::function<void()> select;            // accept
    std::string help;                        // shown under the menu while selected
};

class Menu {
public:
    std::string title, subtitle;
    std::vector<Item> items;
    int sel = 0;

    // Draws one frame and handles input. Returns false when the player pressed cancel (back).
    bool Frame();
    // For remote tests: run an item's action by label prefix. Returns false if not found.
    bool Activate(const std::string &labelPrefix, int dir = 0);

private:
    int holdDir_ = 0, holdStart_ = 0, lastRepeat_ = 0;
};

}  // namespace ui
