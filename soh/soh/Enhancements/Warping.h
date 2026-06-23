#ifndef SOH_WARPING_H
#define SOH_WARPING_H

#include <vector>
#include <string>

// String-based access to the saved warp points (managed in the Enhancements menu),
// so callers like the speedrun menu can list and trigger warps without the WarpPoint type.

// Names of all saved warp points, in sorted (map) order.
std::vector<std::string> GetWarpPointNames();

// Warp to the named saved warp point. Returns false if no such point exists.
bool WarpToNamedPoint(const std::string& name);

// The built-in scene list (no setup required), reused from the debug Map Select.
std::vector<std::string> GetDefaultWarpNames();

// Warp to the built-in destination at `index` (into GetDefaultWarpNames). Returns
// false if out of range.
bool WarpToDefaultIndex(size_t index);

// speedrun-style hierarchical warp browser: category -> place -> entrance. Most categories
// (Dungeons, Towns, Houses, ...) descend cat -> place -> entrance. "Flat" categories
// (Bosses) skip the entrance level: the place row is itself the warp target. The menu
// deals only in (category, place, entrance) ints and the strings these return.
int         SpeedrunWarp_CategoryCount();
const char* SpeedrunWarp_CategoryName(int cat);
bool        SpeedrunWarp_CategoryIsFlat(int cat); // true: the place row warps directly (Bosses)
int         SpeedrunWarp_PlaceCount(int cat);
const char* SpeedrunWarp_PlaceName(int cat, int place);
int         SpeedrunWarp_EntranceCount(int cat, int place);
const char* SpeedrunWarp_EntranceName(int cat, int place, int entrance);
// Warp to (cat, place, entrance). For a flat category, `entrance` is ignored. Returns
// false if the selection is out of range or the warp could not be performed.
bool        SpeedrunWarp_Do(int cat, int place, int entrance);

#endif
