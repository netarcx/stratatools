/*
 * 1-Wire wiring test -- Arduino Nano (ATmega328P)
 *
 * A bench tool for proving the wiring to the cartridge's DS2433 before running
 * the refill firmware at it. It is deliberately a standalone, single-file
 * sketch: install the OneWire library, open this, upload. Nothing else from
 * nano_cartridge_refill is needed.
 *
 * READ-ONLY BY CONSTRUCTION. After a reset this sketch issues only ROM-phase
 * commands -- search, read ROM (0x33), match ROM (0x55), skip ROM (0xCC) -- and
 * exactly one memory-phase command: read memory (0xF0). Write-scratchpad (0x0F)
 * never appears. 0x55 does appear, but only as the ROM command immediately
 * after a reset, where it means match ROM; it is copy-scratchpad only in the
 * memory phase, which this sketch never enters. So no sequence of button
 * presses or bus glitches can modify a cartridge with this sketch loaded.
 *
 * WIRING -- identical to the refill firmware, so a pass here is a pass there:
 *   D7  1-Wire data, 4.7k pull-up to +5V
 *   D9  ACTION button to GND (optional -- tap to re-run the test)
 *   D2  STATUS button to GND (optional -- only reported, not used)
 *   D8  external LED via ~330R to GND
 *   D13 onboard LED, mirrors D8
 *   GND and +5V shared with the cartridge EEPROM
 *
 * WHAT IT TELLS YOU
 *   Serial at 115200 gives a six-stage report with a named cause for each
 *   failure. The LED repeats the result afterwards using the same codes as the
 *   refill firmware, so TROUBLESHOOTING.md applies unchanged:
 *     1-1 no device on the bus     1-2 read failed / unstable
 *     1-3 line held low or another master active
 *     2 slow blinks = everything passed.
 *
 * Re-run the test by tapping ACTION or sending any character on serial.
 */
#include <Arduino.h>
#include <OneWire.h>

// ---- Configuration (matches src/main.cpp) ---------------------------------
static const uint8_t ONEWIRE_PIN = 7;
static const uint8_t BUTTON_PIN  = 9;    // ACTION, to GND, internal pull-up
static const uint8_t STATUS_PIN  = 2;    // STATUS, to GND, internal pull-up
static const uint8_t LED_PIN     = LED_BUILTIN;   // D13
static const uint8_t EXT_LED_PIN = 8;             // external LED
static const bool    LED_ACTIVE_LOW = false;

// The cartridge structure the refill firmware reads. Testing exactly this span
// means a pass here proves the read the firmware actually performs.
static const uint16_t CART_LEN = 113;    // 0x71

static const uint8_t  PRESENCE_TRIES = 20;   // presence pulses per run
static const uint8_t  READ_TRIES     = 8;    // repeated reads for the stability check
static const uint16_t QUIET_MS       = 300;  // matches the firmware's bus guard

// 1-Wire commands used here -- all read-only.
static const uint8_t CMD_READ_ROM    = 0x33;
static const uint8_t CMD_MATCH_ROM   = 0x55;
static const uint8_t CMD_SKIP_ROM    = 0xCC;
static const uint8_t CMD_READ_MEMORY = 0xF0;

OneWire ow(ONEWIRE_PIN);

// ---- State carried between stages ------------------------------------------
static uint8_t  g_rom[8];
static bool     g_haveRom;
static uint8_t  g_devices;
static uint8_t  g_ref[CART_LEN];      // first read, the reference
static uint8_t  g_cmp[CART_LEN];      // each subsequent read
static uint32_t g_loopNs;             // cost of one poll in the rise-time loop
static bool     g_calValid;           // false if the pin would not stay low to be timed
static uint8_t  g_failMajor;
static uint8_t  g_failMinor;
static bool     g_noPresence;   // stage 4 got nothing at all, not merely some

// ---- LED -------------------------------------------------------------------
static void led(bool on) {
    uint8_t level = (on ^ LED_ACTIVE_LOW) ? HIGH : LOW;
    digitalWrite(LED_PIN, level);
    digitalWrite(EXT_LED_PIN, level);
}

static void blink(uint8_t times, uint16_t on_ms, uint16_t off_ms) {
    for (uint8_t i = 0; i < times; i++) { led(true); delay(on_ms); led(false); delay(off_ms); }
}

// Same shape as the refill firmware: MAJOR long blinks, then MINOR short ones.
static void emitCode(uint8_t major, uint8_t minor) {
    blink(major, 700, 300);
    delay(500);
    blink(minor, 150, 250);
}

// ---- Serial helpers --------------------------------------------------------
static void stage(uint8_t n, const __FlashStringHelper *name) {
    Serial.print(F("["));
    Serial.print((unsigned long)n);
    Serial.print(F("/6] "));
    Serial.print(name);
}

static void verdict(const __FlashStringHelper *v, const __FlashStringHelper *detail) {
    Serial.print(v);
    if (detail) { Serial.print(F("  ")); Serial.print(detail); }
    Serial.println();
}

static void hint(const __FlashStringHelper *line) {
    Serial.print(F("        -> "));
    Serial.println(line);
}

// A stage that could not run still gets a line. A gap in the numbering reads as
// a crash; an explicit SKIP reads as "this one had no chance to tell you
// anything", which is what it means.
static void skipped(uint8_t n, const __FlashStringHelper *name) {
    stage(n, name);
    Serial.println(F("SKIP"));
}

static void printPin() {
    Serial.print(F("D"));
    Serial.print((unsigned long)ONEWIRE_PIN);
}

static void printHex8(uint8_t v) {
    if (v < 0x10) Serial.print(F("0"));
    Serial.print(v, HEX);
}

// Remember only the first failure: it is the one that caused the rest.
static void fail(uint8_t major, uint8_t minor) {
    if (!g_failMajor) { g_failMajor = major; g_failMinor = minor; }
}

// ---- Stage 1: idle line level ----------------------------------------------
// With our pin high-impedance an idle 1-Wire bus is held high by the pull-up.
// Anything else means the line cannot carry a transaction at all, so this runs
// first: every later stage would fail for the same single reason.
static bool testIdleLevel() {
    stage(1, F("Idle line level ....... "));
    pinMode(ONEWIRE_PIN, INPUT);      // high-Z, no internal pull-up
    delay(2);

    // Ten samples over ten milliseconds can say "unstable" but nothing more.
    // Sample 5000 times over half a second and record the shape of it: how much
    // of the time it is low, how often it changes, and how long the longest low
    // stretch runs. Those three numbers separate causes that all look identical
    // as a bare "unstable".
    const uint16_t SAMPLES = 5000;          // 100 us apart -> 500 ms
    uint16_t lowCount = 0, transitions = 0, maxLowRun = 0, run = 0;
    uint8_t  last = digitalRead(ONEWIRE_PIN);

    for (uint16_t i = 0; i < SAMPLES; i++) {
        uint8_t now = digitalRead(ONEWIRE_PIN);
        if (now == LOW) {
            lowCount++;
            run++;
            if (run > maxLowRun) maxLowRun = run;
        } else {
            run = 0;
        }
        if (now != last) { transitions++; last = now; }
        delayMicroseconds(100);
    }

    if (lowCount == 0) {
        verdict(F("PASS"), F("line idles HIGH"));
        return true;
    }

    if (lowCount == SAMPLES) {
        verdict(F("FAIL"), F("line sits LOW"));
        hint(F("No pull-up, or the data line is shorted to GND."));
        hint(F("Also: a module left connected but unpowered holds the bus low."));
        hint(F("Check the pull-up goes to +5V, and that the data lead is on"));
        hint(F("the pin named in the header above."));
        fail(1, 3);
        return false;
    }

    verdict(F("FAIL"), F("line is unstable"));
    Serial.print(F("        low "));
    Serial.print((unsigned long)((lowCount * 100UL) / SAMPLES));
    Serial.print(F("% of 500 ms, "));
    Serial.print((unsigned long)transitions);
    Serial.print(F(" transitions, longest low run "));
    Serial.print((unsigned long)maxLowRun);
    Serial.println(F(" x100us"));

    // The three signatures worth telling apart, by what actually produces them.
    if (transitions >= 80 && transitions <= 140 && maxLowRun <= 60) {
        hint(F("~100-120 transitions per 500 ms is mains hum: 50/60 Hz coupling"));
        hint(F("into a high-impedance line. Shorten the leads, or route them"));
        hint(F("away from mains wiring. A lower pull-up would also stiffen it."));
    } else if (maxLowRun >= 100) {
        hint(F("It is held low for long stretches, not glitching. Something is"));
        hint(F("actively pulling it down -- a partial short, or a device whose"));
        hint(F("ground or supply is wrong. Not a noise problem."));
    } else if (transitions <= 2) {
        hint(F("Only one or two changes: this is the line RECOVERING from being"));
        hint(F("held low, not an intermittent contact. If the low run is longer"));
        hint(F("than a few hundred microseconds, something on the bus is drawing"));
        hint(F("current as it comes back -- the classic sign of a device running"));
        hint(F("parasite-powered because its VCC is not connected."));
    } else {
        hint(F("Brief, irregular dropouts on an otherwise high line. That is the"));
        hint(F("signature of an intermittent connection -- a wire not seated,"));
        hint(F("a tired breadboard contact, or a cold joint. Reseat the data"));
        hint(F("lead and the pull-up leg, then run again."));
    }
    fail(1, 3);
    return false;
}

// ---- Stage 2: pull-up strength ---------------------------------------------
// Discharge the line, release it, and count polls until it recovers. A 4.7k
// pull-up wins the race before the first sample completes; a floating line
// never recovers. This is the check for the second most common wiring mistake
// (missing pull-up) and it does not need a scope.
//
// Timed with a tight direct-port read: digitalRead() is far too slow to see the
// difference between a 4.7k pull-up and a much weaker one.
static volatile uint8_t *g_inReg;
static uint8_t g_inMask;

static uint16_t waitHighPolls(uint16_t cap) {
    uint16_t n = 0;
    while (!(*g_inReg & g_inMask)) {
        if (++n >= cap) break;
    }
    return n;
}

// Calibrate against a line we are holding low ourselves, using the very same
// loop -- so the per-poll cost is measured, not guessed.
static void calibrateLoop() {
    const uint16_t CAP = 20000;
    // Calibrate on the LED pin, never on the bus. Timing the loop needs a pin
    // held low for ~14 ms, and on a parasite-powered device that is long enough
    // to starve its reservoir and poison every measurement that follows -- the
    // instrument would be creating the fault it then reports. The loop's cost is
    // a property of the code, not of which port register it happens to read.
    volatile uint8_t *saveReg = g_inReg;
    uint8_t saveMask = g_inMask;
    g_inReg  = portInputRegister(digitalPinToPort(EXT_LED_PIN));
    g_inMask = digitalPinToBitMask(EXT_LED_PIN);
    pinMode(EXT_LED_PIN, OUTPUT);
    digitalWrite(EXT_LED_PIN, LOW);

    uint32_t t0 = micros();
    uint16_t n = waitHighPolls(CAP);
    uint32_t dt = micros() - t0;

    g_inReg  = saveReg;
    g_inMask = saveMask;

    // The whole method assumes the pin stays low while we time it. If it did
    // not, the division yields a meaningless sub-nanosecond figure -- which is
    // exactly what a pin shorted high produces. Flag it rather than print it.
    g_calValid = (n >= CAP);
    g_loopNs = g_calValid ? (dt * 1000UL) / CAP : 0;
    if (g_calValid && g_loopNs == 0) g_loopNs = 1;
}

// ---- Pin integrity ---------------------------------------------------------
// Does the pin follow its own output driver, in both directions, every time?
// A healthy AVR pin does. One whose driver has been damaged -- which is what
// repeatedly sinking a short does -- cannot. This is only meaningful with the
// pin BARE: with anything attached it tests the pin and the wiring together,
// which is what the pre-flight already does.
static bool pinFollowsDriver() {
    uint8_t lowOk = 0, highOk = 0;
    for (uint8_t i = 0; i < 20; i++) {
        pinMode(ONEWIRE_PIN, OUTPUT);
        digitalWrite(ONEWIRE_PIN, LOW);
        delayMicroseconds(20);
        if (!(*g_inReg & g_inMask)) lowOk++;
        digitalWrite(ONEWIRE_PIN, HIGH);
        delayMicroseconds(20);
        if (*g_inReg & g_inMask) highOk++;
    }
    pinMode(ONEWIRE_PIN, INPUT);
    return (lowOk == 20 && highOk == 20);
}

// A single 5 us probe, used to poll for the pin coming free. Kept deliberately
// short: while the fault is still present this is driving into it, so the duty
// cycle is what keeps the exposure negligible.
static bool pinLooksFree() {
    pinMode(ONEWIRE_PIN, OUTPUT);
    digitalWrite(ONEWIRE_PIN, LOW);
    delayMicroseconds(5);
    uint8_t stuck = (*g_inReg & g_inMask) ? 1 : 0;
    pinMode(ONEWIRE_PIN, INPUT);
    return !stuck;
}

// Tells a short apart from a damaged pin, with no meter. Waits for the pin to
// be disconnected, then checks whether it can drive itself once bare.
static void diagnoseStuckPin() {
    Serial.println();
    Serial.println(F("--- STUCK-PIN DIAGNOSIS (30 s) ---"));
    Serial.println(F("Disconnect EVERYTHING from this pin -- the data wire AND"));
    Serial.println(F("the pull-up leg. Leave it bare. Watching for it to come free."));

    unsigned long start = millis();
    bool freed = false;
    while (millis() - start < 30000) {
        if (pinLooksFree()) { freed = true; break; }
        delay(100);                     // 5 us of drive per 100 ms while stuck
    }

    if (!freed) {
        Serial.println(F("Still held high after 30 s."));
        hint(F("If you did disconnect it and it is still high, the output"));
        hint(F("driver itself is damaged -- the pin cannot sink at all."));
        hint(F("If you did not, run again once it is bare."));
        return;
    }

    Serial.print(F("Pin came free at "));
    Serial.print((unsigned long)((millis() - start) / 1000));
    Serial.println(F(" s. Verifying it can drive itself..."));

    if (pinFollowsDriver()) {
        Serial.println(F("VERDICT: the pin is HEALTHY."));
        hint(F("Bare, it follows its own driver low and high, 20 times out of"));
        hint(F("20. So the AVR is fine and the fault is external: something in"));
        hint(F("the harness ties this line to +5V. With the resistor measuring"));
        hint(F("4.7k on its own, look for a SECOND path -- a stray jumper, or"));
        hint(F("the data wire sharing a row with the +5V rail."));
    } else {
        Serial.println(F("VERDICT: the pin is DAMAGED."));
        hint(F("Bare, it still will not follow its own output driver. The"));
        hint(F("driver is blown -- consistent with having sunk a short."));
        hint(F("This pin is dead; the data line has to move to another one."));
    }
}

// ---- Pre-flight: can this pin drive the line at all? -----------------------
// A 1-Wire reset is nothing but a long low pulse, so if something external
// holds the pin high the bus cannot work and every stage fails for that one
// reason. Worse, each reset attempt then sinks current into whatever is doing
// the holding. Check it first, in five microseconds, and refuse to continue.
//
// Driving low is the safe direction to test: into a normal 4.7k pull-up it is
// about 1 mA. Reading high anyway means the pin is tied to something far
// stiffer than a pull-up.
static bool preflightDrive() {
    Serial.print(F("PRE-FLIGHT pin drive ........ "));
    pinMode(ONEWIRE_PIN, OUTPUT);
    digitalWrite(ONEWIRE_PIN, LOW);
    delayMicroseconds(5);
    uint8_t stuck = (*g_inReg & g_inMask) ? 1 : 0;
    pinMode(ONEWIRE_PIN, INPUT);            // release immediately, either way

    if (!stuck) {
        Serial.println(F("PASS  the line follows the pin"));
        return true;
    }
    Serial.print(F("FAIL  "));
    printPin();
    Serial.println(F(" reads HIGH while driven LOW"));
    hint(F("NOT the pull-up: a correct 4.7k sinks ~1mA and the pin still"));
    hint(F("reads LOW. Losing that fight is how 1-Wire works. Holding the pin"));
    hint(F("high needs a path to +5V of tens of ohms or less. Two causes:"));
    hint(F("1. The data wire also lands on +5V, bypassing the resistor."));
    hint(F("2. The resistor is the wrong value -- 4.7k is yellow-violet-RED,"));
    hint(F("   47R is yellow-violet-BLACK, and 47R is ~106mA into the pin."));
    hint(F("Check: power off, meter D7 to +5V. Want ~4.7k. Near 0R is (1),"));
    hint(F("~47R is (2)."));
    hint(F("The bus cannot be driven low, so no reset and no presence pulse"));
    hint(F("are possible and every stage below would fail for this one reason."));
    hint(F("POWER DOWN first: each reset attempt sinks current into it."));
    fail(1, 3);
    return false;
}

static bool testPullup() {
    stage(2, F("Pull-up strength ...... "));

    // Five attempts, keep the fastest: a timer interrupt landing mid-measurement
    // can only ever make one attempt look slower than the line really is.
    const uint16_t CAP = 20000;                 // ~5 ms of polling
    uint16_t best = CAP;
    for (uint8_t i = 0; i < 5; i++) {
        pinMode(ONEWIRE_PIN, OUTPUT);
        digitalWrite(ONEWIRE_PIN, LOW);
        delayMicroseconds(1000);                // drain the line
        pinMode(ONEWIRE_PIN, INPUT);            // release
        uint16_t n = waitHighPolls(CAP);
        if (n < best) best = n;
        // The device reads that low pulse as a reset and answers with a
        // presence pulse. Let it finish before the next attempt.
        delay(2);
    }

    uint32_t riseNs = (uint32_t)best * g_loopNs;

    if (best >= CAP) {
        verdict(F("FAIL"), F("line never recovered"));
        hint(F("Either there is no pull-up, or something is holding the line"));
        hint(F("low -- released, it should snap high in well under a"));
        hint(F("microsecond. Check for 4.7k to +5V and for a short to GND."));
        fail(1, 3);
        return false;
    }

    if (!g_calValid) {
        verdict(F("SKIP"), F("timing calibration was not valid"));
        return true;                    // the pre-flight already named the cause
    }
    if (best == 0) {
        Serial.print(F("PASS  rose within one poll (<"));
        Serial.print((unsigned long)g_loopNs);
        Serial.println(F(" ns)"));
    } else {
        Serial.print(riseNs < 1000 ? F("PASS") : F("WARN"));
        Serial.print(F("  rise ~"));
        Serial.print((unsigned long)riseNs);
        Serial.println(F(" ns"));
    }

    if (riseNs >= 1000) {
        hint(F("Slower than a 4.7k pull-up should be on a short lead."));
        hint(F("Suspect too large a resistor or too much wire capacitance."));
        if (riseNs >= 10000) {
            hint(F("Above ~10us the line is still rising when the device"));
            hint(F("answers a reset -- reads will be unreliable. Fix this first."));
            riseVsLowDuration();
            fail(1, 3);
            return false;
        }
    }
    // Worth saying plainly: this proves a pull-up exists and is fast enough.
    // It cannot measure the resistor -- confirm 4.7k with a meter.
    return true;
}

// ---- Rise vs. low-duration ------------------------------------------------
// Separates the two things that slow a line's recovery, by using the one way
// they differ. Plain capacitance charges in the same time no matter how long
// the line was held down first. A device storing charge off the line does not:
// the longer it is starved, the more it draws back, and the slower the
// recovery. So sweep the low duration and watch which pattern appears.
static void riseVsLowDuration() {
    Serial.println();
    Serial.println(F("--- RISE vs LOW-DURATION ---"));
    Serial.println(F("  held low      rise"));

    static const uint16_t HOLD_US[4] = {100, 1000, 5000, 20000};
    uint32_t riseNs[4];

    for (uint8_t k = 0; k < 4; k++) {
        delay(300);                       // let anything on the bus refill first

        pinMode(ONEWIRE_PIN, OUTPUT);
        digitalWrite(ONEWIRE_PIN, LOW);
        // delayMicroseconds is only good to ~16 ms; build longer holds in steps.
        for (uint16_t left = HOLD_US[k]; left; ) {
            uint16_t chunk = left > 1000 ? 1000 : left;
            delayMicroseconds(chunk);
            left -= chunk;
        }
        pinMode(ONEWIRE_PIN, INPUT);
        uint16_t n = waitHighPolls(20000);
        riseNs[k] = (uint32_t)n * g_loopNs;

        Serial.print(F("  "));
        Serial.print((unsigned long)HOLD_US[k]);
        Serial.print(F(" us"));
        Serial.print(HOLD_US[k] < 1000 ? F("      ") : (HOLD_US[k] < 10000 ? F("     ") : F("    ")));
        Serial.print((unsigned long)riseNs[k]);
        Serial.println(F(" ns"));
    }

    // A fivefold spread across the sweep is far beyond measurement scatter.
    if (riseNs[3] > riseNs[0] * 5 && riseNs[3] > 20000) {
        hint(F("Recovery worsens the longer the line is held down: a device on"));
        hint(F("this bus is PARASITE-POWERED, drawing its charge off the data"));
        hint(F("line. This also proves the data lead AND the ground are sound,"));
        hint(F("since nothing could draw that current otherwise."));
        hint(F("Fine for READS: a reset is a 480us low pulse, far short of the"));
        hint(F("hold time where recovery collapses."));
        hint(F("The concern is WRITES. copy-scratchpad needs ~10ms of strong"));
        hint(F("pull-up, which a 4.7k cannot supply parasitically. If VCC is"));
        hint(F("wired, connect it. If the chip only has two wires, the firmware"));
        hint(F("must drive the line HIGH through the programming window."));
    } else if (riseNs[3] > 20000) {
        hint(F("Slow, but flat across the sweep -- that is bulk capacitance or"));
        hint(F("too high a pull-up value, not a device stealing charge."));
        hint(F("Shorten the leads, or drop the pull-up to 2.2k."));
    } else {
        hint(F("Recovery is fast and flat across the sweep -- nothing is"));
        hint(F("loading this line. This is what a healthy bus looks like."));
    }
}

// ---- Stage 3: bus quiet ----------------------------------------------------
// The same guard the refill firmware runs before any write. Here it answers a
// different question: is anything else (a printer) talking on this bus?
static bool testQuiet() {
    stage(3, F("Bus quiet (300 ms) .... "));
    pinMode(ONEWIRE_PIN, INPUT);
    unsigned long start = millis();
    while (millis() - start < QUIET_MS) {
        if (digitalRead(ONEWIRE_PIN) == LOW) {
            verdict(F("FAIL"), F("something pulled the line low"));
            hint(F("A second master is active -- the printer, most likely."));
            hint(F("Disconnect the cartridge from the machine, or switch to SERVICE."));
            fail(1, 3);
            return false;
        }
        delayMicroseconds(50);          // 1-Wire slots are ~60us; this catches them
    }
    verdict(F("PASS"), F("no other master seen"));
    return true;
}

// ---- Stage 4: presence pulse -----------------------------------------------
// Repeated rather than single, because the failure worth catching here is the
// intermittent one: a marginal crimp or an over-long lead answers most resets
// and drops the rest. A single reset would call that a pass.
static bool testPresence() {
    stage(4, F("Presence pulse x20 .... "));
    uint8_t ok = 0;
    for (uint8_t i = 0; i < PRESENCE_TRIES; i++) {
        if (ow.reset()) ok++;
        delay(10);
    }

    Serial.print(ok == PRESENCE_TRIES ? F("PASS") : F("FAIL"));
    Serial.print(F("  "));
    Serial.print((unsigned long)ok);
    Serial.print(F("/"));
    Serial.println((unsigned long)PRESENCE_TRIES);

    if (ok == PRESENCE_TRIES) return true;

    if (ok == 0) {
        g_noPresence = true;
        hint(F("Nothing answered at all. Note what that rules OUT: a DS2433"));
        hint(F("answers a reset even parasite-powered, so a missing VCC would"));
        hint(F("give marginal writes, not silence. Silence means the chip is"));
        hint(F("not electrically on this bus. In likely order:"));
        hint(F("1. The data lead does not reach the chip -- open wire, wrong"));
        hint(F("   contact on the cartridge, or a SERVICE/RUN switch in RUN."));
        hint(F("2. No shared ground between the Nano and the cartridge EEPROM."));
        hint(F("3. Data lead on the wrong pin -- see the header above."));
        hint(F("Probe mode below tells 1 and 2 apart."));
    } else {
        hint(F("INTERMITTENT -- the worst kind of wiring fault."));
        hint(F("Suspect a cold joint, a loose crimp, or leads that are too long."));
        hint(F("Do not run the refill firmware until this is 20/20."));
    }
    fail(1, 1);
    return false;
}

// ---- Stage 5: ROM search ---------------------------------------------------
static bool testRomSearch() {
    stage(5, F("ROM search ............ "));

    // OneWire::search() latches a last-device flag and skips its whole body
    // while it is set, so the search state must be cleared before every search.
    ow.reset_search();

    uint8_t addr[8];
    uint8_t found = 0;
    bool    crcOk = true;
    g_haveRom = false;
    g_devices = 0;

    while (found < 4 && ow.search(addr)) {
        found++;
        if (found == 1) {
            memcpy(g_rom, addr, 8);
            g_haveRom = true;
        }
        if (OneWire::crc8(addr, 7) != addr[7]) crcOk = false;
    }
    ow.reset_search();
    g_devices = found;

    if (found == 0) {
        verdict(F("FAIL"), F("no device enumerated"));
        hint(F("Presence worked but the search did not, so the bus is live"));
        hint(F("and the timing is marginal: shorten the leads, or check the"));
        hint(F("pull-up. This is a signal-integrity fault, not a missing device."));
        fail(1, 1);
        return false;
    }

    Serial.print(crcOk ? F("PASS") : F("FAIL"));
    Serial.print(F("  "));
    Serial.print((unsigned long)found);
    Serial.println(found == 1 ? F(" device") : F(" devices"));

    // Only the first ROM is kept -- re-enumerating to print them all is not
    // worth the flash. Report that one in full; the count above covers the rest.
    Serial.print(F("        ROM  "));
    for (uint8_t i = 0; i < 8; i++) printHex8(g_rom[i]);
    Serial.print(F("   family 0x"));
    printHex8(g_rom[0]);
    if (g_rom[0] == 0x23) Serial.print(F(" (DS2433, expected)"));
    else                  Serial.print(F(" (NOT a DS2433)"));
    Serial.println();

    if (!crcOk) {
        hint(F("ROM CRC mismatch -- the address came back corrupted."));
        hint(F("The bus is running, but not cleanly. Treat as a wiring fault."));
        fail(1, 1);
        return false;
    }
    if (found > 1) {
        hint(F("More than one device on the bus. The refill firmware talks to"));
        hint(F("whichever it enumerates first -- connect one cartridge only."));
    }
    if (g_rom[0] != 0x23) {
        hint(F("Family code is not 0x23, so this is not the cartridge EEPROM."));
        hint(F("Wiring may still be fine -- but check what is on the bus."));
    }

    // Cross-check with read ROM. It is only valid single-drop, and disagreement
    // is a second, independent way to catch a noisy line.
    if (found == 1) {
        if (ow.reset()) {
            uint8_t rd[8];
            ow.write(CMD_READ_ROM);
            for (uint8_t i = 0; i < 8; i++) rd[i] = ow.read();
            if (memcmp(rd, g_rom, 8) != 0) {
                hint(F("Read ROM disagrees with the search result -- noisy bus."));
                fail(1, 1);
                return false;
            }
        }
    }
    return true;
}

// ---- Stage 6: read stability -----------------------------------------------
// Reads the exact span the refill firmware reads, several times over, and
// compares. One good read proves the wiring can work; eight identical reads
// prove it works reliably, which is the only thing worth trusting a write to.
static bool readCart(uint8_t *dst) {
    if (!ow.reset()) return false;
    if (g_haveRom) {
        ow.write(CMD_MATCH_ROM);
        for (uint8_t i = 0; i < 8; i++) ow.write(g_rom[i]);
    } else {
        ow.write(CMD_SKIP_ROM);         // fallback: still get data to look at
    }
    ow.write(CMD_READ_MEMORY);
    ow.write(0x00);                     // TA1
    ow.write(0x00);                     // TA2
    for (uint16_t i = 0; i < CART_LEN; i++) dst[i] = ow.read();
    return true;
}

static bool testReadStability() {
    stage(6, F("Read stability x8 ..... "));

    if (!readCart(g_ref)) {
        verdict(F("FAIL"), F("first read did not start"));
        hint(F("The reset before the read found no device."));
        fail(1, 2);
        return false;
    }

    uint8_t same = 1;
    for (uint8_t i = 1; i < READ_TRIES; i++) {
        delay(5);
        if (!readCart(g_cmp)) continue;
        if (memcmp(g_cmp, g_ref, CART_LEN) == 0) same++;
    }

    Serial.print(same == READ_TRIES ? F("PASS") : F("FAIL"));
    Serial.print(F("  "));
    Serial.print((unsigned long)same);
    Serial.print(F("/"));
    Serial.print((unsigned long)READ_TRIES);
    Serial.println(F(" identical"));

    if (same != READ_TRIES) {
        hint(F("The same bytes read back differently. The bus works but is"));
        hint(F("not dependable -- shorten leads, check the pull-up and ground."));
        hint(F("A write over a bus this noisy is how a cartridge gets bricked."));
        fail(1, 2);
        return false;
    }

    // All-identical is necessary but not sufficient: a disconnected data line
    // that idles high reads a stable block of 0xFF every time.
    uint8_t first = g_ref[0];
    bool uniform = true;
    for (uint16_t i = 1; i < CART_LEN; i++) {
        if (g_ref[i] != first) { uniform = false; break; }
    }
    if (uniform) {
        Serial.print(F("        WARN: every byte read back as 0x"));
        printHex8(first);
        Serial.println();
        hint(F("A blank or unresponsive device reads uniform. Consistent 0xFF"));
        hint(F("usually means the data line is not actually reaching the chip."));
        fail(1, 2);
        return false;
    }
    return true;
}

// ---- Hex dump --------------------------------------------------------------
// The refill firmware decodes this; here it is raw on purpose. An encrypted
// cartridge image looks like noise -- that is what a correct read looks like.
static void dumpCart() {
    Serial.println(F("--- first 113 bytes as read (encrypted; noise is correct) ---"));
    for (uint16_t i = 0; i < CART_LEN; i += 16) {
        printHex8((uint8_t)i);          // offsets stop at 0x70, so one byte is enough
        Serial.print(F(": "));
        for (uint8_t j = 0; j < 16 && (i + j) < CART_LEN; j++) {
            printHex8(g_ref[i + j]);
            Serial.print(F(" "));
        }
        Serial.println();
    }
}

// ---- Probe mode ------------------------------------------------------------
// The one question stage 1 cannot answer: does the data lead actually reach the
// chip? Our own pull-up holds the pin high either way, so an open lead and a healthy
// bus look identical. A human with a jumper can settle it in a second -- so
// watch the line and report every transition. Touch the data line to GND at the
// CARTRIDGE end: a change here means the wire is continuous all the way back;
// silence means it is not.
//
// Shorting 1-Wire data to GND is exactly what a bus reset does, so this is safe
// on a live cartridge.
static void probeMode() {
    Serial.println();
    Serial.println(F("--- PROBE MODE (20 s) ---"));
    Serial.println(F("Briefly touch the DATA line to GND at the CARTRIDGE end of"));
    Serial.println(F("the harness -- not at the Nano. The LED follows the line."));
    Serial.println(F("Nothing printed = that lead never reaches the Nano."));

    pinMode(ONEWIRE_PIN, INPUT);
    uint8_t  last    = digitalRead(ONEWIRE_PIN);
    uint16_t changes = 0;
    unsigned long start = millis();

    while (millis() - start < 20000) {
        uint8_t now = digitalRead(ONEWIRE_PIN);
        if (now != last) {
            changes++;
            // Contact bounce makes one touch several transitions. That still
            // proves continuity, so keep counting but stop printing.
            if (changes <= 20) {
                Serial.print(F("  line -> "));
                Serial.print(now == LOW ? F("LOW ") : F("HIGH"));
                Serial.print(F("  at "));
                Serial.print((unsigned long)(millis() - start));
                Serial.println(F(" ms"));
            }
            last = now;
        }
        led(now == LOW);            // usable with no console attached
        if (digitalRead(BUTTON_PIN) == LOW) break;   // ACTION skips
        delay(2);
    }
    led(false);

    Serial.print(F("Probe done: "));
    Serial.print((unsigned long)changes);
    Serial.println(F(" transitions."));
    if (changes == 0) {
        hint(F("The line never moved. The data lead does not reach the Nano --"));
        hint(F("an open wire, the wrong cartridge contact, or a switch in RUN."));
        hint(F("Ground is not the suspect yet; fix the data path first."));
    } else {
        hint(F("The data lead is continuous, so the wire itself is fine."));
        hint(F("With presence still at 0/20, the ground return is now the"));
        hint(F("prime suspect -- the Nano and the EEPROM must share GND."));
    }
}

// ---- Test run --------------------------------------------------------------
static void runTests() {
    g_failMajor = 0;
    g_failMinor = 0;
    g_haveRom = false;
    g_noPresence = false;

    Serial.println();
    Serial.println(F("=== 1-Wire wiring test (Arduino Nano) ==="));
    Serial.print(F("Data pin "));
    printPin();
    Serial.println(F(". READ-ONLY: nothing is ever written to the cartridge."));
    Serial.println();

    // Calibrate per run, after the pre-flight has shown the pin can be driven.
    if (!preflightDrive()) {
        skipped(1, F("Idle line level ....... "));
        skipped(2, F("Pull-up strength ...... "));
        skipped(3, F("Bus quiet ............. "));
        skipped(4, F("Presence pulse ........ "));
        skipped(5, F("ROM search ............ "));
        skipped(6, F("Read stability ........ "));
        Serial.println();
        Serial.print(F("RESULT: FAILED -- LED code "));
        Serial.print((unsigned long)g_failMajor);
        Serial.print(F("-"));
        Serial.print((unsigned long)g_failMinor);
        Serial.println(F("  (pin held high)"));
        diagnoseStuckPin();
        return;
    }
    // Order matters here. calibrateLoop() holds the pin LOW for ~14 ms to time
    // its poll loop, which is a long enough low pulse to be a 1-Wire reset --
    // so running it before stage 1 meant stage 1 measured the line's recovery
    // from that pulse instead of its true idle state, and reported the recovery
    // as instability. Stage 1 goes first, after a settle.
    delay(50);

    bool ok = true;
    if (!testIdleLevel()) ok = false;

    calibrateLoop();
    if (!testPullup())    ok = false;

    // Every remaining stage needs a line that idles high. Running them anyway
    // would print a column of failures that all have the one cause already
    // reported, which reads as a worse fault than it is.
    if (!ok) {
        Serial.println(F("        (stages 3-6 need a line that idles high)"));
        skipped(3, F("Bus quiet ............. "));
        skipped(4, F("Presence pulse ........ "));
        skipped(5, F("ROM search ............ "));
        skipped(6, F("Read stability ........ "));
    } else {
        if (!testQuiet()) ok = false;

        if (!testPresence()) {
            // Nothing answers a reset, so a search or a read would only repeat
            // the same finding in a less obvious form.
            ok = false;
            skipped(5, F("ROM search ............ "));
            skipped(6, F("Read stability ........ "));
        } else {
            if (!testRomSearch())     ok = false;
            if (!testReadStability()) ok = false;
            else                      dumpCart();
        }
    }

    Serial.println();
    Serial.print(F("ACTION (D9) reads "));
    Serial.print(digitalRead(BUTTON_PIN) == LOW ? F("LOW (pressed or shorted)") : F("HIGH (idle)"));
    Serial.print(F(" | STATUS (D2) reads "));
    Serial.println(digitalRead(STATUS_PIN) == LOW ? F("LOW (pressed or shorted)") : F("HIGH (idle/unwired)"));

    Serial.println();
    if (ok) {
        Serial.println(F("RESULT: WIRING OK -- safe to load the refill firmware."));
        Serial.println(F("Note: this proves a pull-up is present and fast enough,"));
        Serial.println(F("not that it is 4.7k. Confirm the value with a meter."));
    } else {
        Serial.print(F("RESULT: FAILED -- LED code "));
        Serial.print((unsigned long)g_failMajor);
        Serial.print(F("-"));
        Serial.print((unsigned long)g_failMinor);
        Serial.println(F(" (see TROUBLESHOOTING.md)"));
    }
    Serial.println(F("Tap ACTION or send any character to run again."));

    // Only when it can actually discriminate: the line is healthy but nothing
    // is on it. After any other failure it would just waste twenty seconds.
    if (g_noPresence) probeMode();
}

// Repeat the result on the LED so the test is usable with no console attached.
static void showResult() {
    if (g_failMajor) emitCode(g_failMajor, g_failMinor);
    else             blink(2, 400, 400);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(LED_PIN, OUTPUT);
    pinMode(EXT_LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(STATUS_PIN, INPUT_PULLUP);
    led(false);

    g_inReg  = portInputRegister(digitalPinToPort(ONEWIRE_PIN));
    g_inMask = digitalPinToBitMask(ONEWIRE_PIN);

    // Three quick blinks first: if the LED does not do this, the LED wiring is
    // the fault, and none of the codes below could be read anyway.
    blink(3, 100, 100);

    runTests();
}

void loop() {
    showResult();

    for (uint8_t i = 0; i < 150; i++) {              // 1.5 s, but interruptible
        if (Serial.available()) {
            while (Serial.available()) Serial.read();
            runTests();
            return;
        }
        if (digitalRead(BUTTON_PIN) == LOW) {
            delay(30);                               // debounce
            if (digitalRead(BUTTON_PIN) == LOW) {
                while (digitalRead(BUTTON_PIN) == LOW) delay(10);
                runTests();
                return;
            }
        }
        delay(10);
    }
}
