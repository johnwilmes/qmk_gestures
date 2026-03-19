/* Tap-hold gesture tests.
 *
 * Tests the hold gesture (DEFINE_HOLD).
 * See test_gestures.c for the layout:
 *   (0,0) = KC_A, (0,1) = hold(B/Shift), (0,2) = KC_C, (0,3) = hold(D/Ctrl)
 *
 * Single tap falls through to the base keymap (KC_B / KC_D).
 */

#include "gesture_test.hpp"

class TapHold : public TestFixture {};

/* A plain key (no gesture) should emit immediately. */
TEST_F(TapHold, PlainKeyPassthrough) {
    TestDriver driver;
    auto       keys  = gesture_keymap({{0,0}, {0,1}}, 1);
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

/* Tap: press and release before timeout.
 * Hold gesture fails, buffered events replay → base keymap → KC_B. */
TEST_F(TapHold, Tap) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_th = keys[1];

    /* Press: event buffered, hold gesture goes partial, no report yet */
    EXPECT_NO_REPORT(driver);
    key_th.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release: max_presses=1 reached, hold gesture fails (key not held),
     * buffered events replay through base keymap → KC_B press + release. */
    EXPECT_REPORT(driver, (KC_B));
    EXPECT_EMPTY_REPORT(driver);
    key_th.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Hold: press and hold past timeout.
 * Should produce the hold keycode (KC_LSFT). */
TEST_F(TapHold, Hold) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_th = keys[1];

    key_th.press();
    run_one_scan_loop();

    /* Hold past timeout → shift activates */
    EXPECT_REPORT(driver, (KC_LSFT));
    idle_for(TAPDANCE_TIMEOUT);

    /* Release → shift deactivates */
    EXPECT_EMPTY_REPORT(driver);
    key_th.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Hold then press another key: hold produces modifier,
 * second key is modified. */
TEST_F(TapHold, HoldThenKey) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_a  = keys[0];
    auto&      key_th = keys[1];

    key_th.press();
    run_one_scan_loop();

    /* Hold past timeout → shift activates */
    EXPECT_REPORT(driver, (KC_LSFT));
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Press plain key while shift held → shift + A */
    EXPECT_REPORT(driver, (KC_LSFT, KC_A));
    key_a.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release plain key → shift only */
    EXPECT_REPORT(driver, (KC_LSFT));
    key_a.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release hold → empty */
    EXPECT_EMPTY_REPORT(driver);
    key_th.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Two separate hold keys: hold both as modifiers. */
TEST_F(TapHold, TwoHolds) {
    TestDriver driver;
    auto       keys    = gesture_keymap({{0,0}, {0,1}, {0,2}, {0,3}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_a   = keys[0];
    auto&      key_th1 = keys[1];
    auto&      key_th2 = keys[3];

    /* Hold first (shift) */
    key_th1.press();
    run_one_scan_loop();
    EXPECT_REPORT(driver, (KC_LSFT));
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Hold second (ctrl) while shift held */
    key_th2.press();
    run_one_scan_loop();
    EXPECT_REPORT(driver, (KC_LSFT, KC_LCTL));
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Press plain key → shift + ctrl + A */
    EXPECT_REPORT(driver, (KC_LSFT, KC_LCTL, KC_A));
    key_a.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release all */
    EXPECT_REPORT(driver, (KC_LSFT, KC_LCTL));
    key_a.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    EXPECT_REPORT(driver, (KC_LCTL));
    key_th1.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    EXPECT_EMPTY_REPORT(driver);
    key_th2.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);
}
