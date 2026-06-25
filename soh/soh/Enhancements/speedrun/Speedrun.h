#ifndef SPEEDRUN_H
#define SPEEDRUN_H

#ifdef __cplusplus
extern "C" {
#endif

// True while a Speedrun-mode save is loaded. Gameplay-affecting settings are locked to the
// speedrun preset while this is active so every runner shares the same ruleset.
bool Speedrun_IsLockActive(void);

#ifdef __cplusplus
}
#endif

#endif // SPEEDRUN_H
