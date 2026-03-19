/* Basic tests: no gestures, key events pass straight through. */

#include "gesture_test.hpp"

class GestureBasic : public TestFixture {};

TEST_F(GestureBasic, SingleKeyPress) {
    TestDriver driver;
    auto       keys  = gesture_keymap({{0,0}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_a = keys[0];

    EXPECT_REPORT(driver, (KC_A));
    key_a.press();
    run_one_scan_loop();

    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

TEST_F(GestureBasic, TwoKeysSequential) {
    TestDriver driver;
    auto       keys  = gesture_keymap({{0,0}, {0,1}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_a = keys[0];
    auto&      key_b = keys[1];

    EXPECT_REPORT(driver, (KC_A));
    key_a.press();
    run_one_scan_loop();

    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();

    EXPECT_REPORT(driver, (KC_B));
    key_b.press();
    run_one_scan_loop();

    EXPECT_EMPTY_REPORT(driver);
    key_b.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}
