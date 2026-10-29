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

}  // namespace amped3::scripted_input
