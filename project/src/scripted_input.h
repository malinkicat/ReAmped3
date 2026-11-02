// scripted_input.h - Test-only virtual controller driven by a command file
//
// With --scripted_input=true, input_script.txt next to the executable is read,
// deleted and played back as button presses on a synthetic pad that the input
// system merges into player 1. One command per line:
//   start | a | b | x | y | back | up | down | left | right | lb | rb  [hold_ms]
//   wait <ms>
// It works while the game window is unfocused, unlike keyboard emulation.

#pragma once

#include <rex/input/input_system.h>

#include <memory>

namespace amped3::scripted_input {

std::unique_ptr<rex::input::InputDriver> CreateDriver();

// The scripted controller's buttons held now (0 without --scripted_input).
uint16_t HeldButtons();

// Presses `buttons` `count` times on the virtual pad (always present; the
// script file is only read with --scripted_input), e.g. B to back out of the
// game's menus after a quick save.
void Tap(uint16_t buttons, int count);

// Holds the left stick at (x, y) for half a second, then presses `buttons`
// with it held (e.g. picking an item on the pause wheel).
void TapStick(float x, float y, uint16_t buttons);

// Drops the virtual pad's queued presses and releases its buttons.
void Cancel();

// Nothing queued on the virtual pad.
bool Idle();

}  // namespace amped3::scripted_input
