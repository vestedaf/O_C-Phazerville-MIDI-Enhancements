// Copyright (c) 2026, Andrew Desena
// Siggy: Combo ProbDiv + ProbMeloD applet for O_C
// Inspired by Stochastic Instruments' Stochastic Inspiration Generator
//
// Left half: ProbDiv (weighted probability divider)
// Right half: ProbMeloD (weighted probability melody)
// Both share a ProbLoopLinker for synchronized looping
// Outputs: DAC0 = pitch CV (from ProbMeloD), DAC1 = gate voltage (from ProbDiv)
// Bonus: Accent probability → velocity, CV-controllable octave shift

#pragma once
#include "../HSProbLoopLinker.h"
#include "../HemisphereApplet.h"

class Siggy : public HemisphereApplet {
public:

    // Cursor layout for normal (half-screen) mode
    enum SiggyCursor {
        // Page 0: ProbDiv
        WEIGHT1, WEIGHT2, WEIGHT4, WEIGHT8,
        LOOP_LENGTH,
        DIV_CV_MODE,
        // Page 1: ProbMeloD
        MELO_LOWER, MELO_UPPER,
        MELO_ROTATE,
        MELO_CV_MODE,
        // Page 1: Shared features
        ACCENT_PROB,
        QSELECT,
        DUR_SHORT, DUR_MID, DUR_LONG,
        LAST_CURSOR = DUR_LONG
    };

    static constexpr uint8_t PAGE_DIV = 0;
    static constexpr uint8_t PAGE_MELO = 1;
    static constexpr uint8_t LAST_PAGE = PAGE_MELO;

    // CV input modes for ProbDiv
    enum DivCvMode {
        DIV_CV_NONE,
        DIV_CV_LOOP_LENGTH,    // CV1 → loop length
        DIV_CV_RESEED,         // CV2 → reseed threshold
        DIV_CV_LOOP_LENGTH_CV2_RESEED, // CV1 → loop, CV2 → reseed
        DIV_CV_LAST
    };

    static constexpr uint8_t MAX_WEIGHT = 15;
    static constexpr uint8_t MAX_LOOP_LENGTH = 32;
    static constexpr uint8_t MAX_MELO_WEIGHT = 10;
    static constexpr uint8_t MAX_MELO_RANGE = 31;
    static constexpr uint8_t MAX_DUR_WEIGHT = 7;

    const char* applet_name() {
        return "Siggy";
    }
    const uint8_t* applet_icon() { return PhzIcons::probDiv; }

    void Start() {
        // ProbDiv state
        weight_1 = 8;
        weight_2 = 4;
        weight_4 = 2;
        weight_8 = 1;
        loop_length = 0;
        loop_index = 0;
        loop_step = 0;
        skip_steps = 0;
        bypass_loop = false;
        div_cv_mode = DIV_CV_LOOP_LENGTH_CV2_RESEED;

        // ProbMeloD state
        down = 1;
        up = 7;
        pitch[0] = 0;
        pitch[1] = 0;
        current_note_degree = 0;
        current_note_semitone = 0;
        melo_cv_mode = 0;
        for (int i = 0; i < 7; i++) {
            degree_weights[i] = 3;
        }
        degree_weights[0] = 7;   // root
        degree_weights[3] = 5;   // 4th
        degree_weights[4] = 5;   // 5th

        // Shared
        accent_prob = 3;    // ~50% chance (0-7 scale)
        accent_cv = 0;
        qselect = io_offset;
        dur_short = 3; dur_mid = 5; dur_long = 2;

        // Output state
        curr_pitch_cv = 0;
        curr_gate_cv = 0;
        gate_off_tick = 0;
        picked_gate_ratio = 4;

        page_ = PAGE_DIV;
    }

    void Controller() {
        const uint32_t this_tick = OC::CORE::ticks;

        // === ProbDiv logic (left side) ===
        loop_linker.RegisterDiv(hemisphere);

        // CV1 → loop length modulation
        loop_length_mod = loop_length;
        if (DetentedIn(0)) {
            Modulate(loop_length_mod, 0, 0, MAX_LOOP_LENGTH);
        }

        loop_linker.SetLooping(loop_length_mod > 0 && !bypass_loop);

        // Reset on Clock(1)
        if (Clock(1)) {
            loop_step = 0;
            loop_index = 0;
            skip_steps = 0;
            reset_animation = HEMISPHERE_PULSE_ANIMATION_TIME_LONG;
        }

        // Clock(0) → ProbDiv trigger
        if (Clock(0)) {
            uint32_t cycle_time = ClockCycleTicks(0);

            int reseed = DetentedIn(1);
            // Reseed on CV2 rising above 2.5V
            if (reseed > (5*ONE_OCTAVE >> 1) && !reseed_high) {
                GenerateDivLoop(true, false);
                loop_linker.TriggerRegeneration();
                reseed_high = true;
                reseed_animation = HEMISPHERE_PULSE_ANIMATION_TIME_LONG;
            }
            if (reseed < (5*ONE_OCTAVE >> 1) && reseed_high) {
                reseed_high = false;
            }

            // Bypass loop if CV2 < -2.5V
            bypass_loop = (reseed < -(5*ONE_OCTAVE >> 1));

            // Loop wrap
            if (loop_length_mod > 0 && loop_step >= loop_length_mod) {
                loop_step = 0;
                loop_index = 0;
                skip_steps = 0;
                reset_animation = HEMISPHERE_PULSE_ANIMATION_TIME_LONG;
            }

            loop_linker.SetLoopStep(loop_index);

            // Division logic
            if (--skip_steps > 0) {
                if (loop_length_mod > 0) loop_step++;
                loop_linker.Trigger(1);
                // Skip: no gate, no new pitch — just keep linker in sync
                goto output_and_animate;;
            }

            if (loop_length_mod > 0) {
                skip_steps = GetNextLoopDiv();
            }
            if (loop_length_mod == 0 || bypass_loop) {
                skip_steps = GetNextWeightedDiv();
            }

            if (skip_steps == 0) goto output_and_animate;;

            // New note fires
            loop_linker.Trigger(0);
            curr_pitch_cv = FireMelody(0);
            // Gate voltage: 3V normal, 5V accent (like TB-3PO)
            curr_gate_cv = (accent_cv > 100) ? HEMISPHERE_3V_CV * 5 / 3 : HEMISPHERE_3V_CV;
            gate_off_tick = this_tick + (cycle_time >> 1);
        }

        // === Auto gate-off (like TB3PO gate timing) ===
        if (curr_gate_cv > 0 && gate_off_tick > 0 && this_tick >= gate_off_tick) {
            gate_off_tick = 0;
            curr_gate_cv = 0;
        }

        // === ProbMeloD logic (right side) ===
        loop_linker.RegisterMelo(hemisphere);

        // CV modulation: CV inputs alter degree range
        down_mod = constrain(down + (DetentedIn(0) >> 8), 1, 7);
        up_mod = constrain(up + (DetentedIn(1) >> 8), down_mod, 7);

        // Reseed from ProbDiv
        regen = regen || loop_linker.ShouldRegenerate();
        regen = regen || (loop_linker.IsLooping() && (down_mod != old_down || up_mod != old_up));
        old_down = down_mod;
        old_up = up_mod;

        if (regen) {
            regen = false;
            GenerateMeloLoop();
        }

    output_and_animate:
        // Single output pair per tick — TB3PO convention: DAC0=pitch, DAC1=gate
        Out(0, curr_pitch_cv);
        Out(1, curr_gate_cv);

        // Animate
        if (pulse_animation > 0) pulse_animation--;
        if (reseed_animation > 0) reseed_animation--;
        if (reset_animation > 0) reset_animation--;
        if (value_animation > 0) value_animation--;
    }

    // Normal hemisphere view: one page at a time (full 64×64)
    void View() {
        if (page_ == PAGE_DIV) {
            DrawDivSide();
        } else {
            DrawMeloSide();
        }
        // Page indicator icon at top-right
        if (page_ == PAGE_DIV) {
            gfxIcon(56, 1, LEFT_ICON);  // left arrow = Div page
        } else {
            gfxIcon(56, 1, RIGHT_ICON); // right arrow = MeloD page
        }
    }

    // Full-screen view: side-by-side (left 64px = Div, right 64px = MeloD)
    void DrawFullScreen() {
        drawing_fullscreen_ = true;
        DrawDivSide();
        DrawMeloSide();
        drawing_fullscreen_ = false;
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            // Page-specific cursor navigation with page transitions
            if (page_ == PAGE_DIV) {
                // Div page: WEIGHT1..DIV_CV_MODE
                if (direction > 0 && cursor >= DIV_CV_MODE) {
                    // Scroll past last Div param → go to MeloD page
                    page_ = PAGE_MELO;
                    cursor = MELO_LOWER;
                    ResetCursor();
                    return;
                }
                if (direction < 0 && cursor <= WEIGHT1) {
                    // Stay at first param (no previous page)
                    return;
                }
                MoveCursor(cursor, direction, DIV_CV_MODE);
            } else {
                // MeloD page: MELO_LOWER..DUR_LONG
                if (direction > 0 && cursor >= DUR_LONG) {
                    // Stay at last param (no next page)
                    return;
                }
                if (direction < 0 && cursor <= MELO_LOWER) {
                    // Scroll before first MeloD param → go back to Div page
                    page_ = PAGE_DIV;
                    cursor = DIV_CV_MODE;
                    ResetCursor();
                    return;
                }
                MoveCursor(cursor, direction, DUR_LONG);
            }
            return;
        }

        switch ((SiggyCursor)cursor) {
        case WEIGHT1: weight_1 = constrain(weight_1 + direction, 0, MAX_WEIGHT); break;
        case WEIGHT2: weight_2 = constrain(weight_2 + direction, 0, MAX_WEIGHT); break;
        case WEIGHT4: weight_4 = constrain(weight_4 + direction, 0, MAX_WEIGHT); break;
        case WEIGHT8: weight_8 = constrain(weight_8 + direction, 0, MAX_WEIGHT); break;
        case LOOP_LENGTH: {
            int old = loop_length;
            loop_length = constrain(loop_length + direction, 0, MAX_LOOP_LENGTH);
            if (old == 0 && loop_length > 0) {
                GenerateDivLoop(true, true);
                loop_linker.TriggerRegeneration();
            }
            break;
        }
        case DIV_CV_MODE:
            div_cv_mode = constrain(div_cv_mode + direction, 0, DIV_CV_LAST - 1);
            break;
        case MELO_LOWER:
            down = constrain(down + direction, 1, up);
            break;
        case MELO_UPPER:
            up = constrain(up + direction, down, MAX_MELO_RANGE);
            break;
        case MELO_ROTATE:
            rotate_masked_left(degree_weights, 0x7f, 7, -direction);
            break;
        case MELO_CV_MODE:
            melo_cv_mode = constrain(melo_cv_mode + direction, 0, (int)(std::size(probmelod::cv_modes) - 1));
            break;
        case ACCENT_PROB:
            accent_prob = constrain(accent_prob + direction, 0, 7);
            break;
        case QSELECT:
            qselect = constrain(qselect + direction, 0, 7);
            break;
        case DUR_SHORT:
            dur_short = constrain(dur_short + direction, 0, MAX_DUR_WEIGHT);
            break;
        case DUR_MID:
            dur_mid = constrain(dur_mid + direction, 0, MAX_DUR_WEIGHT);
            break;
        case DUR_LONG:
            dur_long = constrain(dur_long + direction, 0, MAX_DUR_WEIGHT);
            break;
        default: break;
        }

        if (cursor < DIV_CV_MODE && loop_length > 0) {
            GenerateDivLoop(false, false);
        }
        if (loop_linker.IsLooping()) {
            regen = true;
        }
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation{0,4}, weight_1);
        Pack(data, PackLocation{4,4}, weight_2);
        Pack(data, PackLocation{8,4}, weight_4);
        Pack(data, PackLocation{12,4}, weight_8);
        Pack(data, PackLocation{16,5}, loop_length);
        Pack(data, PackLocation{21,12}, loop_linker.GetSeed());
        Pack(data, PackLocation{33,3}, div_cv_mode);
        Pack(data, PackLocation{36,5}, down);
        Pack(data, PackLocation{41,5}, up);
        Pack(data, PackLocation{46,3}, melo_cv_mode);
        Pack(data, PackLocation{49,3}, accent_prob);
        Pack(data, PackLocation{52,3}, qselect);
        Pack(data, PackLocation{55,3}, dur_short);
        Pack(data, PackLocation{58,3}, dur_mid);
        Pack(data, PackLocation{61,3}, dur_long);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        weight_1 = Unpack(data, PackLocation{0,4});
        weight_2 = Unpack(data, PackLocation{4,4});
        weight_4 = Unpack(data, PackLocation{8,4});
        weight_8 = Unpack(data, PackLocation{12,4});
        loop_length = Unpack(data, PackLocation{16,5});
        loop_linker.SetSeed(Unpack(data, PackLocation{21,12}));
        div_cv_mode = Unpack(data, PackLocation{33,3});
        down = constrain(Unpack(data, PackLocation{36,5}), 1, MAX_MELO_RANGE);
        up = constrain(Unpack(data, PackLocation{41,5}), down, MAX_MELO_RANGE);
        melo_cv_mode = Unpack(data, PackLocation{46,3});
        accent_prob = Unpack(data, PackLocation{49,3});
        qselect = Unpack(data, PackLocation{52,3});
        dur_short = Unpack(data, PackLocation{55,3});
        dur_mid = Unpack(data, PackLocation{58,3});
        dur_long = Unpack(data, PackLocation{61,3});
        if (loop_length > 0) GenerateDivLoop(false, true);
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Clock";
        help[HELP_DIGITAL2] = "Reset";
        help[HELP_CV1]      = "Length";
        help[HELP_CV2]      = "Reseed";
        help[HELP_OUT1]     = "Pitch";
        help[HELP_OUT2]     = "Gate";
        help[HELP_EXTRA1]   = "Siggy: Div+Melo";
        help[HELP_EXTRA2]   = "Stochastic Insp";
    }

private:
    // ProbDiv state
    int cursor;
    int weight_1, weight_2, weight_4, weight_8;
    int loop_length, loop_length_mod;
    int div_loop[MAX_LOOP_LENGTH];
    int loop_index, loop_step;
    int skip_steps;
    bool reseed_high, bypass_loop;
    int div_cv_mode;

    // ProbMeloD state
    int8_t degree_weights[7] = {7, 3, 3, 5, 5, 3, 3};
    int8_t down, down_mod, up, up_mod;
    uint8_t pitch[2] = {0};
    uint8_t seqloop[2][32];
    uint8_t current_note_degree = 0;
    uint8_t current_note_semitone = 0;
    int8_t melo_cv_mode = 0;
    bool regen = false;
    int old_down = 1, old_up = 7;

    // Shared
    uint8_t accent_prob;
    int accent_cv;
    int qselect = 0;
    uint8_t dur_short = 3, dur_mid = 5, dur_long = 2;

    // Output state (TB3PO convention: DAC0=pitch, DAC1=gate)
    int curr_pitch_cv = 0;
    int curr_gate_cv = 0;
    uint32_t gate_off_tick = 0;
    uint8_t picked_gate_ratio = 4;

    // Animation
    int pulse_animation = 0;
    int reseed_animation = 0;
    int reset_animation = 0;
    int value_animation = 0;

    // Page state
    uint8_t page_ = PAGE_DIV;
    bool drawing_fullscreen_ = false;

    ProbLoopLinker &loop_linker = ProbLoopLinker::get();

    const int *weights_ptr[4] = {&weight_1, &weight_2, &weight_4, &weight_8};
    const int divs[4] = {1, 2, 4, 8};

    // === ProbDiv methods ===
    int PickGateRatio() {
        int total = dur_short + dur_mid + dur_long;
        if (total == 0) return 4;  // default: mid
        int rnd = random(0, total);
        if (rnd < dur_short) return 1;   // 1/16 gate
        if (rnd < dur_short + dur_mid) return 4;  // 4/16 gate
        return 16;  // full cycle gate
    }

    int FireMelody(int ch) {
        if (loop_linker.IsLooping()) {
            pitch[ch] = seqloop[ch][loop_linker.GetLoopStep()];
        } else {
            pitch[ch] = GetNextWeightedPitch();
        }

        // Accent: probabilistic velocity accent
        bool do_accent = false;
        if (accent_prob > 0) {
            int rnd = random(0, 8);
            do_accent = (rnd < accent_prob);
        }

        // Pick gate duration for this note
        picked_gate_ratio = PickGateRatio();

        // Quantize through global quantizer (like TB3PO)
        int midi_note = 60 + pitch[ch];
        midi_note = constrain(midi_note, 0, 127);
        int quantized_cv = HS::QuantizerLookup(qselect, midi_note);

        // Store note info for display
        current_note_degree = pitch[ch];
        current_note_semitone = MIDIQuantizer::NoteNumber(quantized_cv) % 12;

        accent_cv = do_accent ? 127 : 64;
        return quantized_cv;
    }

    int GetNextWeightedDiv() {
        int total_weights = 0;
        for (int i = 0; i < 4; i++) total_weights += *weights_ptr[i];
        int rnd = random(0, total_weights + 1);
        for (int i = 0; i < 4; i++) {
            if (rnd <= *weights_ptr[i] && *weights_ptr[i] > 0) return divs[i];
            rnd -= *weights_ptr[i];
        }
        return 0;
    }

    void GenerateDivLoop(bool reseed, bool restart) {
        memset(div_loop, 0, sizeof(div_loop));
        if (restart) {
            loop_step = 0;
            loop_index = 0;
        }
        int index = 0;
        int counter = 0;
        if (reseed) {
            randomSeed(micros());
            loop_linker.SetSeed(random(0, 65535));
        }
        int full_seed = (loop_linker.GetSeed() << 16) |
                        (weight_1 << 12) | (weight_2 << 8) |
                        (weight_4 << 4) | weight_8;
        randomSeed(full_seed);
        while (counter < MAX_LOOP_LENGTH) {
            int div = GetNextWeightedDiv();
            if (div == 0) break;
            div_loop[index] = div;
            index++;
            counter += div;
        }
    }

    int GetNextLoopDiv() {
        int value = div_loop[loop_index];
        if (value == 0) {
            loop_index = 0;
            loop_step = 0;
            value = div_loop[loop_index];
        }
        loop_index++;
        loop_step++;
        return value;
    }

    // === ProbMeloD methods ===
    template <typename T>
    static void rotate_masked_left(T* arr, uint32_t mask, int n, int r) {
        if (n < 32) mask = mask & ~(~0u << n);
        if (!mask) return;
        int count = __builtin_popcount(mask);
        r = (r % count + count) % count;
        if (r == 0) return;
        int j = __builtin_ctz(mask);
        for (int i = 0; i < r; ++i) j += __builtin_ctz(mask >> (j + 1)) + 1;
        int i = __builtin_ctz(mask);
        for (int c = 0; c < count - r; ++c) {
            std::swap(arr[i], arr[j]);
            uint16_t jm = mask >> (++j);
            j = jm ? j + __builtin_ctz(jm) : __builtin_ctz(mask);
            i += __builtin_ctz(mask >> (i + 1)) + 1;
        }
        int m = count % r;
        if (m) rotate_masked_left(arr + i, mask >> i, n - i, -m);
    }

    uint8_t GetNextWeightedPitch() {
        int total_weights = 0;
        int8_t* w = degree_weights;
        for (int i = 0; i < 7; ++i) total_weights += max(0, (int)w[i]);
        if (total_weights == 0) return 3;  // default: 4th degree
        int rnd = random(0, total_weights);
        for (int i = 0; i < 7; ++i) {
            int wi = max(0, (int)w[i]);
            if (rnd < wi && wi > 0) return i;
            rnd -= wi;
        }
        return 0;
    }

    void GenerateMeloLoop() {
        int full_seed = 0;
        for (int p = 0; p < 7; p++) {
            full_seed ^= (degree_weights[p] + 1) << p;
        }
        full_seed ^= (up_mod << 3) | down_mod;
        full_seed |= (loop_linker.GetSeed() << 16);
        randomSeed(full_seed);
        for (int i = 0; i < 32; ++i) {
            seqloop[0][i] = GetNextWeightedPitch();
            seqloop[1][i] = GetNextWeightedPitch();
        }
    }
 
    // === Display: side-by-side ===
    // Left half (x 0-63): ProbDiv
    // Right half (x 64-127): ProbMeloD

    // Check if we're being called in full-screen mode (128×64) or normal mode (64×64)
    bool IsFullScreen() const {
        return drawing_fullscreen_;
    }

    void DrawDivSide() {
        // Division weights with rhythmic visualization
        for (int i = 0; i < 4; i++) {
            int y = 14 + (i * 11);  // rows at y=14,25,36,47

            // Division label
            gfxPrint(1, y, "/");
            gfxPrint(7, y, divs[i]);

            // 8-step rhythm grid — each step is 3px (2px pulse + 1px gap)
            for (int s = 0; s < 8; s++) {
                int sx = 17 + (s * 3);
                bool fires = (s % divs[i] == 0);
                if (fires) {
                    gfxRect(sx, y + 2, 2, 4);  // tall thin pulse marker
                }
            }

            // Probability bar
            int bar_x = 46;
            int bar_w = (*weights_ptr[i] * 12) / MAX_WEIGHT;
            gfxFrame(bar_x, y + 1, 12, 6);
            if (bar_w > 0) {
                gfxRect(bar_x, y + 1, bar_w, 6);
            }

            // Row highlight when active
            if (cursor == i) gfxInvert(1, y, 60, 8);

            // Gate activity — brief invert on the label area when this division fires
            if (pulse_animation > 0 && skip_steps == divs[i]) {
                gfxInvert(1, y, 14, 8);
            }
        }

        // Bottom status row
        // Loop indicator
        gfxIcon(4, 55, LOOP_ICON);
        if (reseed_animation > 0) gfxInvert(4, 55, 12, 8);
        if (loop_length_mod == 0) {
            gfxPrint(19, 55, "off");
        } else {
            gfxPrint(19, 55, loop_length_mod);
        }
        if (cursor == LOOP_LENGTH) gfxCursor(19, 63, 16);

        if (reset_animation > 0) gfxPrint(45, 55, "R");

        // Gate output indicator — active when curr_gate_cv > 0
        if (curr_gate_cv > 0) {
            gfxRect(54, 56, 6, 5);
        }

        // CV mode indicator
        if (cursor == DIV_CV_MODE) {
            gfxPos(1, 4);
            gfxPrint("CV:");
            gfxPrint(18, 4, div_cv_mode < DIV_CV_LAST ? "L" : "?");
        }
    }

    void DrawMeloSide() {
        int ox = drawing_fullscreen_ ? 64 : 0;

        // --- 7-position scale degree wheel (open center) ---
        static const int8_t kDx[7] = {0, 11, 14, 6, -6, -14, -11};
        static const int8_t kDy[7] = {-14, -9, 3, 13, 13, 3, -9};
        static const int8_t kLx[7] = {0, 14, 18, 7, -7, -18, -14};
        static const int8_t kLy[7] = {-18, -11, 4, 17, 17, 4, -11};
        static const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

        const uint8_t cx = ox + 32;
        const uint8_t cy = 28;

        // Rim outline — no spokes, open center
        gfxCircle(cx, cy, 14);

        // Endpoint dots at each degree position (size = weight proportion)
        for (uint8_t i = 0; i < 7; ++i) {
            int w = max(0, (int)degree_weights[i]);
            if (w > 0) {
                int ex = cx + (kDx[i] * w / 7);
                int ey = cy + (kDy[i] * w / 7);
                // Short anchor line from endpoint toward center (connects dot to wheel)
                gfxLine(ex, ey, ex - (kDx[i] * w / 28), ey - (kDy[i] * w / 28));
                gfxRect(ex - 1, ey - 1, 3, 3);
            }
        }

        // Active degree highlight (outline square at spoke endpoint)
        int act = constrain(current_note_degree, 0, 6);
        if (degree_weights[act] > 0) {
            int ax = cx + (kDx[act] * degree_weights[act] / 7);
            int ay = cy + (kDy[act] * degree_weights[act] / 7);
            gfxFrame(ax - 2, ay - 2, 5, 5);
        }

        // Degree number labels (1-7) outside the rim
        static const char* kDegLabels[7] = {"1", "2", "3", "4", "5", "6", "7"};
        for (uint8_t i = 0; i < 7; ++i) {
            int lx = cx + kLx[i];
            int ly = cy + kLy[i];
            uint8_t ly2 = (ly >= 44) ? ly - 8 : ly;
            gfxPrint(lx, ly2, kDegLabels[i]);
        }

        // Note name in center (plain text, no invert box)
        const char* nn = kNoteNames[current_note_semitone];
        gfxPrint(cx - 4, cy - 3, nn);
        // Active degree: show which degree number is firing below the name
        char deg_str[2] = {(char)('1' + constrain(current_note_degree, 0, 6)), '\0'};
        gfxPrint(cx - 3, cy + 4, deg_str);

        // --- Status area (all y ≤ 63 safe) ---
        // Row 1: Quantizer + Accent
        gfxPrint(ox + 1, 47, "Q");
        gfxPrint(ox + 7, 47, qselect);
        gfxPrint(ox + 18, 47, "A");
        gfxPrint(ox + 24, 47, accent_prob);
        // Invert highlights instead of gfxCursor (avoids off-screen)
        if (cursor == QSELECT) gfxInvert(ox + 1, 47, 14, 8);
        if (cursor == ACCENT_PROB) gfxInvert(ox + 18, 47, 14, 8);
        if (cursor == MELO_CV_MODE) {
            gfxPrint(ox + 36, 47, "CV");
            gfxInvert(ox + 36, 47, 14, 8);
        }

        // Row 2: Duration weights
        gfxPrint(ox + 1, 55, "S");
        gfxPrint(ox + 7, 55, dur_short);
        gfxPrint(ox + 15, 55, "M");
        gfxPrint(ox + 21, 55, dur_mid);
        gfxPrint(ox + 29, 55, "L");
        gfxPrint(ox + 35, 55, dur_long);
        if (cursor == DUR_SHORT) gfxInvert(ox + 1, 55, 14, 8);
        if (cursor == DUR_MID) gfxInvert(ox + 15, 55, 14, 8);
        if (cursor == DUR_LONG) gfxInvert(ox + 29, 55, 14, 8);
    }
};
