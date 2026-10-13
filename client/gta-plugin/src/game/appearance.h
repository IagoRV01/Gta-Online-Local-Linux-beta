// Applies a character::Character to a freemode ped and reads state back from it.
#pragma once
#include <vector>
#include "common/character.h"

namespace appearance {

void ApplyLook(int ped, const character::Character &c);     // head blend, face, hair, overlays, clothes, props, tattoos
void ApplyState(int ped, const character::Character &c);    // health, armour, weapons
void CaptureState(int ped, character::Character &c);        // health, armour, weapons, position (clothes are owned by the profile)

std::vector<character::Weapon> CaptureWeapons(int ped);
void GiveWeapons(int ped, const std::vector<character::Weapon> &w);   // removes current weapons first

// Counts for the creator (depend on the ped's model and the installed DLCs).
int DrawableCount(int ped, int component);
int TextureCount(int ped, int component, int drawable);
int PropCount(int ped, int anchor);
int PropTextureCount(int ped, int anchor, int drawable);
int OverlayCount(int overlay);
int HairTintCount();

}  // namespace appearance
