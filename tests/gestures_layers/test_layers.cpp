/* Layer system tests.
 *
 * Tests layer resolution, MO(n), TG(n), and transparency.
 * See test_gestures.c for the layout:
 *   (0,0) = KC_A / KC_1 / KC_F1
 *   (0,1) = KC_B / KC_2 / KC_F2
 *   (0,2) = KC_C / KC_TRNS / -
 *   (1,0) = taphold: tap=KC_TAB, hold=MO(1)
 *   (1,1) = taphold: tap=KC_ESC, hold=TG(2)
 */

#include "gesture_test.hpp"

class Layers : public TestFixture {};

/* Base layer key resolution. */
TEST_F(Layers, BaseLayer) {
    TestDriver driver;
    auto keys = gesture_keymap({{0,0}, {0,1}, {0,2}, {0,3},
                                {1,0}, {1,1}}, 3);
    SET_GESTURE_KEYMAP(keys);
    auto& key_a = keys[0];
    auto& key_b = keys[1];

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

/* MO(1): hold layer key, press another key, verify layer 1 keycode. */
TEST_F(Layers, MomentaryLayer) {
    TestDriver driver;
    auto keys = gesture_keymap({{0,0}, {0,1}, {0,2}, {0,3},
                                {1,0}, {1,1}}, 3);
    SET_GESTURE_KEYMAP(keys);
    auto& key_a  = keys[0];
    auto& key_mo = keys[4];

    /* Hold MO(1) key past timeout → layer 1 activates (no report for MO) */
    key_mo.press();
    run_one_scan_loop();
    EXPECT_NO_REPORT(driver);
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Press (0,0) while layer 1 active → KC_1 */
    EXPECT_REPORT(driver, (KC_1));
    key_a.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release (0,0) → empty */
    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release MO key → layer 1 deactivates */
    EXPECT_NO_REPORT(driver);
    key_mo.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Press (0,0) again → back to KC_A */
    EXPECT_REPORT(driver, (KC_A));
    key_a.press();
    run_one_scan_loop();

    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* KC_TRNS on layer 1 falls through to layer 0. */
TEST_F(Layers, Transparency) {
    TestDriver driver;
    auto keys = gesture_keymap({{0,0}, {0,1}, {0,2}, {0,3},
                                {1,0}, {1,1}}, 3);
    SET_GESTURE_KEYMAP(keys);
    auto& key_c  = keys[2];
    auto& key_mo = keys[4];

    /* Activate layer 1 */
    key_mo.press();
    run_one_scan_loop();
    idle_for(TAPDANCE_TIMEOUT);

    /* (0,2) is KC_TRNS on layer 1 → falls through to KC_C from layer 0 */
    EXPECT_REPORT(driver, (KC_C));
    key_c.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    EXPECT_EMPTY_REPORT(driver);
    key_c.release();
    run_one_scan_loop();

    key_mo.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* TG(2): toggle layer on/off. */
TEST_F(Layers, ToggleLayer) {
    TestDriver driver;
    auto keys = gesture_keymap({{0,0}, {0,1}, {0,2}, {0,3},
                                {1,0}, {1,1}}, 3);
    SET_GESTURE_KEYMAP(keys);
    auto& key_a  = keys[0];
    auto& key_tg = keys[5];

    /* Hold TG(2) key past timeout → layer 2 toggles on */
    key_tg.press();
    run_one_scan_loop();
    EXPECT_NO_REPORT(driver);
    idle_for(TAPDANCE_TIMEOUT);
    VERIFY_AND_CLEAR(driver);

    /* Release TG key (layer 2 stays on — it was toggled) */
    key_tg.release();
    run_one_scan_loop();

    /* (0,0) → KC_F1 from layer 2 */
    EXPECT_REPORT(driver, (KC_F1));
    key_a.press();
    run_one_scan_loop();

    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Toggle layer 2 off again */
    key_tg.press();
    run_one_scan_loop();
    idle_for(TAPDANCE_TIMEOUT);
    key_tg.release();
    run_one_scan_loop();

    /* (0,0) → back to KC_A */
    EXPECT_REPORT(driver, (KC_A));
    key_a.press();
    run_one_scan_loop();

    EXPECT_EMPTY_REPORT(driver);
    key_a.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}

/* Tap the MO key → should produce KC_TAB (tap behavior). */
TEST_F(Layers, MoTap) {
    TestDriver driver;
    auto keys = gesture_keymap({{0,0}, {0,1}, {0,2}, {0,3},
                                {1,0}, {1,1}}, 3);
    SET_GESTURE_KEYMAP(keys);
    auto& key_mo = keys[4];

    EXPECT_NO_REPORT(driver);
    key_mo.press();
    run_one_scan_loop();
    VERIFY_AND_CLEAR(driver);

    /* Release: max_presses=1 reached, hold fails, events replay → KC_TAB. */
    EXPECT_REPORT(driver, (KC_TAB));
    EXPECT_EMPTY_REPORT(driver);
    key_mo.release();
    run_one_scan_loop();

    VERIFY_AND_CLEAR(driver);
}
