/* Tap-dance gesture tests.
 *
 * Tests multi-tap and tap-then-hold with DEFINE_TAPDANCE (max_presses=2).
 * See test_gestures.c for the layout:
 *   (0,0) = KC_A (plain)
 *   (0,1) = tapdance: tap → KC_B (base), double-tap → KC_X, hold → KC_LSFT,
 *                      tap-then-hold → KC_LCTL
 *   (0,2) = KC_C (plain)
 */

#include "gesture_test.hpp"

class TapDance : public TestFixture {};

/* Single tap: all gestures fail, events replay → base keymap → KC_B.
 * Must wait for timeout since max_presses > 1. */
TEST_F(TapDance, SingleTap) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}, {0,2}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_td = keys[1];

    /* Press and release */
    EXPECT_NO_REPORT(driver);
    key_td.press();
    run_one_scan_loop();
    key_td.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Wait for timeout: no more taps → all gestures fail → replay → KC_B */
    EXPECT_REPORT(driver, (KC_B));
    EXPECT_EMPTY_REPORT(driver);
    idle_for(TAPDANCE_TIMEOUT);

    VERIFY_AND_CLEAR(driver);
}

/* Double tap: press-release-press-release → KC_X.
 * Max_presses=2 reached on second release → resolves immediately. */
TEST_F(TapDance, DoubleTap) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}, {0,2}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_td = keys[1];

    /* First tap */
    EXPECT_NO_REPORT(driver);
    key_td.press();
    run_one_scan_loop();
    key_td.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Second tap: max_presses reached → tap(2) activates immediately */
    EXPECT_NO_REPORT(driver);
    key_td.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    EXPECT_REPORT(driver, (KC_X));
    EXPECT_EMPTY_REPORT(driver);
    key_td.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Hold on first press → KC_LSFT. */
TEST_F(TapDance, Hold) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}, {0,2}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_td = keys[1];

    key_td.press();
    run_one_scan_loop();

    /* Hold past timeout → shift activates */
    EXPECT_REPORT(driver, (KC_LSFT));
    idle_for(TAPDANCE_TIMEOUT);

    /* Release → shift deactivates */
    EXPECT_EMPTY_REPORT(driver);
    key_td.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Tap then hold on second press → KC_LCTL. */
TEST_F(TapDance, TapThenHold) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}, {0,2}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_td = keys[1];

    /* First tap */
    EXPECT_NO_REPORT(driver);
    key_td.press();
    run_one_scan_loop();
    key_td.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Second press: hold past timeout → ctrl activates */
    key_td.press();
    run_one_scan_loop();

    EXPECT_REPORT(driver, (KC_LCTL));
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Release → ctrl deactivates */
    EXPECT_EMPTY_REPORT(driver);
    key_td.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Hold then press another key while modifier active. */
TEST_F(TapDance, HoldThenKey) {
    TestDriver driver;
    auto       keys   = gesture_keymap({{0,0}, {0,1}, {0,2}}, 1);
    SET_GESTURE_KEYMAP(keys);
    auto&      key_a  = keys[0];
    auto&      key_td = keys[1];

    /* Hold → shift */
    key_td.press();
    run_one_scan_loop();
    EXPECT_REPORT(driver, (KC_LSFT));
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Press A while shift held → shift + A */
    EXPECT_REPORT(driver, (KC_LSFT, KC_A));
    key_a.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release A → shift only */
    EXPECT_REPORT(driver, (KC_LSFT));
    key_a.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release shift → empty */
    EXPECT_EMPTY_REPORT(driver);
    key_td.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}
