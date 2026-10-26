// GBA Cart Player � plays a Game Boy Advance cartridge's own music, clocked and mangled by the rack
//
// Boots a Game Boy Advance over the pulse jacks via BIOS Multiboot WITH A CARTRIDGE INSERTED (the
// console is powered on holding START+SELECT, so the BIOS skips the cart and waits for multiboot).
// The payload scans the cart for the MP2K ("Sappy"/m4a) sound engine's song data and plays it
// through its own player: audio out of the GBA's headphone jack, the song and its tracks on the
// GBA's screen.
//
// THIS FIRMWARE IS THE FM CARD'S (and 108's) BUT FOR TWO THINGS: it announces GBA_CAP_UP_PULSE and
// turns GBA_UP_PULSE - the song's beat and loop counters - into pulses on the audio outputs. The
// link, the protocol and every other message are unchanged; the player lives entirely on the GBA
// side (payload/), which is what the division of labour below makes possible.
//
//   Pulse Out 1 (GPIO 8) -> GBA SC  (serial clock, master-driven)
//   Pulse Out 2 (GPIO 9) -> GBA SI  (MOSI)
//   Pulse In  1 (GPIO 2) -> GBA SO  (MISO)
//   (common ground)
//
// THE DIVISION OF LABOUR: the Workshop senses, the GBA is the instrument. This side reads the
// five inputs and streams them down raw; the GBA owns the clocking, the modulation matrix and the
// UI. That is what makes the on-screen editor free � re-mapping an input is a change to a table in
// GBA RAM, not a protocol change and not a firmware rebuild.
//
// Panel (what each input MEANS is decided on the GBA - see payload/mod.h; this is the factory map):
//   Pulse In 2   clock (PPQN on the GBA)   Audio In 1   reset        Audio In 2   sample start
//   CV In 1      voice shift               CV In 2      transpose, 1V/oct
//   Main knob    layers (track muting)     Knob X       tempo        Knob Y       ROM scrub
//   CV Out 2     followed track's pitch, calibrated 1V/oct
//   CV Out 1     followed track's gate, 5 V
//   Audio Out 1  beat pulse (one per quarter note of the song)
//   Audio Out 2  loop pulse (each time the song loops)
//
// Architecture: ComputerCard's 48 kHz audio/CV loop runs on core 0 (ProcessSample below). The
// entire GBA link engine runs on core 1 (gba_link_core1) and communicates only through the
// lock-free GbaShared struct � the audio path is never blocked by the link. This mirrors
// 96_cathode's core-0/core-1 split for the pulse pins.

#include "ComputerCard.h"
#include "pico/multicore.h"

#include "gba_link.h"
#include "gba_proto.h"
#include "patch_store.h"
#include "gba_multiboot.h"

#include "pico/flash.h"
#include "gba_payload.h"   // baked multiboot .mb image: gba_payload[] / gba_payload_size

// ─────────────────────────────────────────────────────────────────────────────
// Core 1 trampoline — hands the baked payload to the link engine.
// ─────────────────────────────────────────────────────────────────────────────
static void core1_entry()
{
    gba_link_core1(gba_payload, gba_payload_size);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputerCard subclass — Core 0 (48 kHz)
// ─────────────────────────────────────────────────────────────────────────────
class GbaCard : public ComputerCard
{
public:
    GbaCard()
    {
        // Tell the GBA whether the quantised pitch out can be trusted, BEFORE core 1 starts:
        // the EEPROM calibration is read by ComputerCard's own constructor, which has already
        // run by the time we get here, and the link sends its first HELLO within milliseconds
        // of being launched below.
        gGba.caps      = (CVOutsCalibrated() ? GBA_CAP_CVOUT_CAL : 0) | GBA_CAP_AUD_EDGES
                       | GBA_CAP_UP_PULSE;
        gGba.patchMask = patch_store_used();

        // Core 1 is NOT launched here any more — see StartLink(). The flash lockout has to be
        // armed on core 0 first, and that has to happen before anything on core 1 could ask for
        // a patch save.
    }

    // Launch the link engine on core 1. Core 1 immediately reassigns GPIO 8/9/2 to PIO;
    // ComputerCard's own gpio use of those pins (PulseOut1/2, PulseIn1) is then simply superseded
    // on the pad. Core 1's PIO/DMA claims don't collide with ComputerCard, which claims its
    // resources later inside Run() (see 96_cathode for the same pattern).
    void StartLink() { multicore_launch_core1(core1_entry); }

    void __not_in_flash_func(ProcessSample)() override
    {
        // ---- Workshop -> GBA : the five inputs ----
        // Signed -2048..2047 biased to 12-bit unsigned for the wire. The GBA subtracts 2048
        // again, so 0 V is 0 V at both ends and there is exactly one place the convention
        // lives (gba_proto.h).
        gGba.inputs[GBA_IN_CV1]  = (uint16_t)(CVIn1()    + 2048);
        gGba.inputs[GBA_IN_CV2]  = (uint16_t)(CVIn2()    + 2048);
        gGba.inputs[GBA_IN_AUD1] = (uint16_t)(AudioIn1() + 2048);
        gGba.inputs[GBA_IN_AUD2] = (uint16_t)(AudioIn2() + 2048);

        gGba.gate = PulseIn2() ? 1 : 0;

        // A monotonic counter, not a sticky flag: core 1 is the only reader and it advances
        // its own tally by one per report, so a trigger far shorter than the 1 ms poll can
        // neither be lost to a clear-vs-set race nor collapsed into its neighbour.
        if (PulseIn2RisingEdge()) gGba.gateEdges = gGba.gateEdges + 1;

        // Audio In 1/2 as trigger inputs, sensed here at 48 kHz because the stream only carries
        // them as ~440 Hz snapshots (see GBA_OP_AUDEDGE). A Schmitt trigger - up through about
        // +1 V, back down through about +0.5 V - so a slow or noisy edge counts once. This is
        // sensing, not a musical decision: what an edge DOES is still entirely the GBA's business.
        {
            int16_t a[2] = { AudioIn1(), AudioIn2() };
            uint8_t lv = gGba.audLevel;
            for (int i = 0; i < 2; i++) {
                uint8_t bit = (uint8_t)(1u << i);
                if (!(lv & bit) && a[i] > 341) {
                    lv |= bit;
                    gGba.audEdges[i] = gGba.audEdges[i] + 1;
                } else if ((lv & bit) && a[i] < 170) {
                    lv &= (uint8_t)~bit;
                }
            }
            gGba.audLevel = lv;
        }

        gGba.knobs[0] = (uint16_t)KnobVal(Knob::Main);
        gGba.knobs[1] = (uint16_t)KnobVal(Knob::X);
        gGba.knobs[2] = (uint16_t)KnobVal(Knob::Y);
        gGba.switchPos = (uint8_t)SwitchVal();

        // ---- GBA -> Workshop : the followed track ----
        // CVOut2MIDINote is CALIBRATED from the module's EEPROM, so the pitch out is in tune with
        // the rest of the rack without any trimming here. The GBA drops the gate for 3 ms between
        // legato notes, so an envelope downstream retriggers on every note.
        CVOut2MIDINote(gGba.note);
        CVOut1Millivolts(gGba.noteVel ? 5000 : 0);

        // ---- the song's beat and loop, as pulses ----
        // The GBA sends COUNTERS; one pulse per increment. A jump of more than 4 is a resync (the
        // link just came up, or a song change reset the count), not a burst of real beats, so it
        // is absorbed silently. A pulse that lands while one is still high simply extends it.
        PulseFromCounter(gGba.beatCount, lastBeat_, beatPulse_);
        PulseFromCounter(gGba.loopCount, lastLoop_, loopPulse_);
        AudioOut1(beatPulse_ ? 2000 : 0);
        AudioOut2(loopPulse_ ? 2000 : 0);
        if (beatPulse_) beatPulse_--;
        if (loopPulse_) loopPulse_--;

        // ---- status on the LEDs ----
        // While the link is up these are instrument state. While it is NOT, they become a
        // diagnostic readout instead — because "LED 0 is blinking" was true for uploading,
        // waiting for a console, and every kind of failure alike, which is no use at all when
        // the thing you need to know is which of those is happening.
        bool booted = (gGba.state == LinkState::Booted);
        if (booted) {
            LedOn(0, true);
            LedOn(1, gGba.gate);                       // clock input
            LedOn(2, gGba.noteVel != 0);               // followed track's gate
            LedOn(3, beatPulse_ != 0 || beatFlash_);   // beat
            LedOn(4, loopFlash_ != 0);                 // loop
            LedOn(5, gGba.mode == GBA_MODE_EDIT);
            if (beatPulse_) beatFlash_ = kFlash;       // stretch a 5 ms pulse into a visible blink
            else if (beatFlash_) beatFlash_--;
            if (loopPulse_) loopFlash_ = kFlash * 4;
            else if (loopFlash_) loopFlash_--;
        } else {
            LedOn(0, (tick_ >> 12) & 1);               // blinking: not booted
            uint32_t p = gba_mb_progress;
            if (p > 0) {
                // Uploading: LEDs 1-5 are a five-segment progress bar. A 37 kB payload is five
                // and a half seconds of transfer, and a bar moving is the difference between
                // "wait" and "something is wrong".
                for (int i = 0; i < 5; i++) LedOn(1 + i, (int)p >= (i + 1) * 20);
            } else {
                // Idle or failed: LEDs 1-3 are the last MultibootResult as a 3-bit code.
                //   1 NoGBA (nothing answered)   2 BadHandshake   3 TransferError
                //   4 CrcMismatch                5 BadPayload
                uint8_t e = gGba.lastError;
                LedOn(1, e & 1);
                LedOn(2, e & 2);
                LedOn(3, e & 4);
                LedOn(4, false);
                LedOn(5, false);
            }
        }

        tick_++;
    }

private:
    static constexpr uint32_t kPulse = 240;           // 5 ms at 48 kHz
    static constexpr uint32_t kFlash = 2400;          // 50 ms

    static void PulseFromCounter(uint8_t now, uint8_t &last, uint32_t &pulse)
    {
        uint8_t d = (uint8_t)(now - last);
        if (d == 0) return;
        last = now;
        if (d <= 4) pulse = kPulse;
    }

    uint32_t tick_ = 0;
    uint8_t  lastBeat_ = 0, lastLoop_ = 0;
    uint32_t beatPulse_ = 0, loopPulse_ = 0;
    uint32_t beatFlash_ = 0, loopFlash_ = 0;
};

int main()
{
    // 144 MHz — a clean multiple of 48 MHz (as 96_cathode uses), giving tidy PIO clkdivs.
    set_sys_clock_khz(144000, true);

    GbaCard card;
    card.EnableNormalisationProbe();   // so an unpatched input reads 0 V instead of noise

    // Launch core 1 FIRST, then arm the flash lockout. THE ORDER MATTERS AND GETTING IT WRONG
    // IS SILENT.
    //
    // multicore_lockout_victim_init() installs an exclusive handler on core 0's inter-core FIFO
    // interrupt. multicore_launch_core1() also uses that FIFO, for its startup handshake — so
    // arming the lockout first means the handler eats the handshake replies and core 0 blocks
    // for ever inside the launch, never reaching Run(). Core 1 still comes up and the GBA still
    // boots and plays, which is what makes it so misleading: the console looks alive while the
    // module's 48 kHz loop has never run once, so every knob, the switch and every CV input sit
    // frozen at their power-on values.
    //
    // Armed after the launch, the lockout does its real job: a patch save erases and reprograms
    // a flash sector, which stops XIP, and any code running from flash on either core during
    // that window crashes the chip. Core 0 lives in ComputerCard's Run() loop, which is in
    // flash. This parks it in a RAM-resident handler for the few milliseconds the write takes —
    // and because the audio interrupt does not run during the lockout, a save clicks. That is
    // the price of a deliberate action, and the reason this is not done from the audio path.
    card.StartLink();
    multicore_lockout_victim_init();

    // GPIO 2/8/9 are owned by the PIO link engine, not by ComputerCard's pulse I/O.
    card.Run();   // blocking; core 0 runs the 48 kHz loop, core 1 runs the GBA link
}
