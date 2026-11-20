// controls.h's arbiter against a simulated CX60 (cx60.h): max on the max timer, boosts while the week program sets
// the mode, rejected modes, a max timer someone else started. Needs the Modbus library, see the Makefile.
//   make -C tests controls_test
#include <cstdio>
#include <cstdlib>
#include "cx60.h"

namespace controls = flexit::controls;

static void run(uint32_t s) {
  for (uint32_t i = 0; i < s; i++)
    cx60_second();
}

// Boots with the CX60's mode setting at `setting` and the week program at `week_program` (MODE_STOP = no week program)
static void start(uint16_t setting, uint16_t week_program) {
  boot();
  mode_on_panel(setting);
  cx60.week_program = week_program;
  run(10);
}

int main() {
  log_on = getenv("LOG") != nullptr;

  // Max runs on the timer and is restarted, the mode setting is never changed
  start(MODE_NORMAL, MODE_STOP);
  controls::request("test", MODE_MAX);
  run(5);
  CHECK(cx60.mode == MODE_MAX && cx60.timer_s > 0);
  CHECK(cx60.timer_minutes == controls::TIMER_MINUTES);
  run(40 * 60);
  CHECK(cx60.mode == MODE_MAX && cx60.setting == MODE_NORMAL);
  CHECK(controls::requested("test") == MODE_MAX);
  controls::release("test");
  run(5);
  CHECK(cx60.mode == MODE_NORMAL && cx60.timer_s == 0 && cx60.timer_minutes == 180);

  // Week program at Min: the timer still runs max, and the boost ends in Min
  start(MODE_NORMAL, MODE_MIN);
  CHECK(cx60.mode == MODE_MIN);
  controls::request("test", MODE_MAX);
  run(5);
  CHECK(cx60.mode == MODE_MAX);
  run(15 * 60);
  CHECK(cx60.mode == MODE_MAX && controls::requested("test") == MODE_MAX);
  controls::release("test");
  run(5);
  CHECK(cx60.mode == MODE_MIN && cx60.timer_s == 0 && cx60.setting == MODE_NORMAL && cx60.timer_minutes == 180);
  cx60.week_program = MODE_STOP;
  run(5);
  CHECK(cx60.mode == MODE_NORMAL);

  // Week program at Min: Normal is rejected and taken back, the request stays, and it's retried later
  start(MODE_MIN, MODE_MIN);
  controls::request("test", MODE_NORMAL);
  run(40);
  CHECK(cx60.mode == MODE_MIN && cx60.setting == MODE_MIN);
  CHECK(controls::requested("test") == MODE_NORMAL);
  run(10 * 60);
  CHECK(controls::requested("test") == MODE_NORMAL);
  cx60.week_program = MODE_STOP;
  run(5);
  CHECK(cx60.mode == MODE_MIN);  // the mode didn't change, so only the 30 min retry gets it there
  run(controls::RETRY_MS / 1000);
  CHECK(cx60.mode == MODE_NORMAL && controls::requested("test") == MODE_NORMAL);
  controls::release("test");
  run(5);
  CHECK(cx60.mode == MODE_MIN && cx60.setting == MODE_MIN);

  // The retry also comes when the mode changes, here the week program going to Normal
  start(MODE_MIN, MODE_NORMAL);
  cx60.week_program = MODE_MIN;
  run(5);
  controls::request("test", MODE_NORMAL);
  run(60);
  CHECK(cx60.mode == MODE_MIN);
  cx60.week_program = MODE_STOP;
  run(5);
  CHECK(cx60.mode == MODE_MIN);
  cx60.week_program = MODE_NORMAL;
  run(5);
  CHECK(cx60.mode == MODE_NORMAL);
  cx60.week_program = MODE_STOP;
  run(40);
  CHECK(cx60.mode == MODE_NORMAL && controls::requested("test") == MODE_NORMAL);

  // A max timer someone else started is neither restarted nor stopped
  start(MODE_NORMAL, MODE_STOP);
  cx60.timer_s = 3 * 3600;
  run(5);
  controls::request("test", MODE_MAX);
  run(60);
  CHECK(cx60.mode == MODE_MAX && cx60.timer_s > 3 * 3600 - 120 && cx60.timer_minutes == 180);
  controls::release("test");
  run(60);
  CHECK(cx60.mode == MODE_MAX && cx60.timer_s > 0);

  // If it ends while the boost runs, ours starts
  start(MODE_NORMAL, MODE_STOP);
  cx60.timer_s = 60;
  run(5);
  controls::request("test", MODE_MAX);
  run(2 * 60);
  CHECK(cx60.mode == MODE_MAX && cx60.timer_minutes == controls::TIMER_MINUTES);
  controls::release("test");
  run(5);
  CHECK(cx60.mode == MODE_NORMAL && cx60.timer_s == 0);

  // Our timer stopped on a panel counts as a change by hand: the boosts are dropped
  start(MODE_NORMAL, MODE_STOP);
  controls::request("test", MODE_MAX);
  run(60);
  cx60.timer_s = 0;
  run(5);
  CHECK(cx60.mode == MODE_NORMAL && controls::requested("test") < 0 && cx60.timer_minutes == 180);

  // So does a mode set on a panel, and our timer is stopped
  start(MODE_NORMAL, MODE_STOP);
  controls::request("test", MODE_MAX);
  run(60);
  mode_on_panel(MODE_MIN);
  run(5);
  CHECK(cx60.mode == MODE_MIN && controls::requested("test") < 0 && cx60.timer_s == 0);

  // Normal and Max requested, then only Normal: the timer stops and Normal is sent
  start(MODE_MIN, MODE_STOP);
  controls::request("a", MODE_NORMAL);
  controls::request("b", MODE_MAX);
  run(10);
  CHECK(cx60.mode == MODE_MAX && cx60.setting == MODE_MIN);
  controls::release("b");
  run(10);
  CHECK(cx60.mode == MODE_NORMAL && cx60.timer_s == 0 && controls::requested("a") == MODE_NORMAL);
  controls::release("a");
  run(10);
  CHECK(cx60.mode == MODE_MIN && cx60.setting == MODE_MIN);

  // Max Timer Speed set to Normal on a panel: the boost sets it to Max meanwhile and puts Normal back after
  start(MODE_NORMAL, MODE_STOP);
  cx60.timer_speed = MODE_NORMAL;
  hear(fc65(REG_CMD_MAX_TIMER_SPEED, MODE_NORMAL));
  controls::request("test", MODE_MAX);
  run(10);
  CHECK(cx60.mode == MODE_MAX && cx60.timer_speed == MODE_MAX);
  controls::release("test");
  run(10);
  CHECK(cx60.mode == MODE_NORMAL && cx60.timer_speed == MODE_NORMAL);

  // Nothing answers at address 1, so the timer doesn't start: after 30 s max is sent as the mode
  start(MODE_NORMAL, MODE_STOP);
  cx60.no_address_1 = true;
  controls::request("test", MODE_MAX);
  run(10);
  CHECK(cx60.mode == MODE_NORMAL);
  run(30);
  CHECK(cx60.mode == MODE_MAX && cx60.timer_s == 0 && controls::requested("test") == MODE_MAX);
  run(10 * 60);
  CHECK(cx60.mode == MODE_MAX && controls::requested("test") == MODE_MAX);
  controls::release("test");
  run(10);
  CHECK(cx60.mode == MODE_NORMAL && cx60.setting == MODE_NORMAL && cx60.timer_minutes == 180 &&
        cx60.timer_speed == MODE_MAX);

  printf("controls test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
