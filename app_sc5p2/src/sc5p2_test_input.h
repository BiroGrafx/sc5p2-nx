// Space Channel 5 Part 2: a scripted controller for test runs (no window focus or keyboard needed).
//
// Off unless one of these is set; then a virtual pad is added next to the real ones and every device feeds
// player 1 (the game is single player):
//   sc5p2_test_buttons       = "T:btn,T1-T2:btn+btn,..."   presses at T seconds after start (0.15 s each),
//                                                           or held from T1 to T2
//   sc5p2_test_buttons_file  = <path>                      live: each line appended to the file is pressed
//                                                           when read, "btn[+btn][:hold seconds]"
// Buttons: a b x y start back up down left right lb rb lt rt ls rs.

#pragma once

#include <memory>

#include <rex/input/input_system.h>

namespace sc5p2::test_input {

// The game's input system: the SDK's default drivers, plus the scripted pad when a test script is set.
std::unique_ptr<rex::system::IInputSystem> Create(bool tool_mode);

}  // namespace sc5p2::test_input
