// Copyright (c) 2026, Andrew Desena
// Siggy: Combo ProbDiv + ProbMeloD applet for O_C
// Inspired by Stochastic Instruments' Stochastic Inspiration Generator
//
// Left half: ProbDiv (weighted probability divider)
// Right half: ProbMeloD (weighted probability melody)
// Both share a ProbLoopLinker for synchronized looping
// Outputs: DAC ch0 = gate (from ProbDiv), DAC ch1 = pitch CV (from ProbMeloD)
// Bonus: Accent probability → velocity, CV-controllable octave shift

#pragma once
#include "../HSProbLoopLinker.h"
#include "../HemisphereApplet.h"

class Siggy : public HemisphereApplet {
public:

    // Cursor layout: Div weights (4) + loop_length + div_cv_mode + melo_low + melo_high + melo_rotate + melo_cv_mode + accent + octave_prob
    enum SiggyCursor {
        // ProbDiv side
        WEIGHT1, WEIGHT2, WEIGHT4, WEIGHT8,
        LOOP_LENGTH,
        DIV_CV_MODE,      // CV input assignment for ProbDiv
        // ProbMeloD side
        MELO_LOWER, MELO_UPPER,
        MELO_ROTATE,
        MELO_CV_MODE,
        // Shared features
        ACCENT_PROB,      // Probability of accent (0-15)
        OCTAVE_PROB,      // Probability of octave shift (0-15)
        LAST_CURSOR = OCTAVE_PROB
    };

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
    static constexpr uint8_t MAX_MELO_RANGE = 60;

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
        up = 12;
        pitch[0] = 0;
        pitch[1] = 0;
        melo_cv_mode = 0;
        for (int i = 0; i < 12; i++) {
            weights[i] = 10;
        }

        // Shared
        accent_prob = 4;    // ~25% chance
        octave_prob = 2;    // ~12% chance
        accent_cv = 0;
        octave_cv = 0;

        ForEachChannel(ch) {
            GateOut(ch, false);
        }
    }

    void Controller() {
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
                ClockOut(1);  // Skip indicator
                loop_linker.Trigger(1);
                // Still fire ProbMeloD on skip
                FireMelody(1);
                return;
            }

            if (loop_length_mod > 0) {
                skip_steps = GetNextLoopDiv();
            }
            if (loop_length_mod == 0 || bypass_loop) {
                skip_steps = GetNextWeightedDiv();
            }

            if (skip_steps == 0) return;

            ClockOut(0);  // Main clock output
            loop_linker.Trigger(0);
            FireMelody(0);
        }

        // === ProbMeloD logic (right side) ===
        loop_linker.RegisterMelo(hemisphere);

        // CV modulation for melody range
        down_mod = down;
        up_mod = up;
        uint8_t cvm = probmelod::cv_modes[melo_cv_mode].cv_config;
        rotation[0] = semitone_cv_in((cvm >> 6) & 0b11);
        rotation[1] = semitone_cv_in((cvm >> 4) & 0b11);
        down_mod = constrain(down + semitone_cv_in((cvm >> 2) & 0b11), 1, up);
        up_mod = constrain(up + semitone_cv_in(cvm & 0b11), down_mod, MAX_MELO_RANGE);

        // Reseed from ProbDiv
        regen = regen || loop_linker.ShouldRegenerate();
        regen = regen || (loop_linker.IsLooping() && (down_mod != old_down || up_mod != old_up));
        old_down = down_mod;
        old_up = up_mod;

        if (regen) {
            regen = false;
            GenerateMeloLoop();
        }

        // Animate
        if (pulse_animation > 0) pulse_animation--;
        if (reseed_animation > 0) reseed_animation--;
        if (reset_animation > 0) reset_animation--;
        if (value_animation > 0) value_animation--;
    }

    void View() {
        DrawDivSide();
        DrawMeloSide();
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor, direction, LAST_CURSOR);
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
            rotate_masked_left(weights, 0xffff, 12, -direction);
            break;
        case MELO_CV_MODE:
            melo_cv_mode = constrain(melo_cv_mode + direction, 0, (int)(std::size(probmelod::cv_modes) - 1));
            break;
        case ACCENT_PROB:
            accent_prob = constrain(accent_prob + direction, 0, 15);
            break;
        case OCTAVE_PROB:
            octave_prob = constrain(octave_prob + direction, 0, 15);
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
        Pack(data, PackLocation{16,8}, loop_length);
        Pack(data, PackLocation{24,16}, loop_linker.GetSeed());
        Pack(data, PackLocation{40,4}, div_cv_mode);
        Pack(data, PackLocation{44,6}, down);
        Pack(data, PackLocation{50,6}, up);
        Pack(data, PackLocation{56,4}, melo_cv_mode);
        Pack(data, PackLocation{60,4}, accent_prob);
        // octave_prob at bit 64 — need more space, skip for now
        return data;
    }

    void OnDataReceive(uint64_t data) {
        weight_1 = Unpack(data, PackLocation{0,4});
        weight_2 = Unpack(data, PackLocation{4,4});
        weight_4 = Unpack(data, PackLocation{8,4});
        weight_8 = Unpack(data, PackLocation{12,4});
        loop_length = Unpack(data, PackLocation{16,8});
        loop_linker.SetSeed(Unpack(data, PackLocation{24,16}));
        div_cv_mode = Unpack(data, PackLocation{40,4});
        down = constrain(Unpack(data, PackLocation{44,6}), 1, 60);
        up = constrain(Unpack(data, PackLocation{50,6}), down, 60);
        melo_cv_mode = Unpack(data, PackLocation{56,4});
        accent_prob = Unpack(data, PackLocation{60,4});
        if (loop_length > 0) GenerateDivLoop(false, true);
    }

protected:
    void SetHelp() {
        help[HELP_DIGITAL1] = "Clock";
        help[HELP_DIGITAL2] = "Reset";
        help[HELP_CV1]      = "Length";
        help[HELP_CV2]      = "Reseed";
        help[HELP_OUT1]     = "Gate";
        help[HELP_OUT2]     = "Pitch";
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
    int8_t weights[12] = {10,10,10,10,10,10,10,10,10,10,10,10};
    int8_t down, down_mod, up, up_mod;
    uint8_t pitch[2] = {0};
    uint8_t seqloop[2][32];
    int8_t rotation[2] = {0};
    int8_t melo_cv_mode = 0;
    bool regen = false;
    int old_down = 1, old_up = 12;

    // Shared
    uint8_t accent_prob, octave_prob;
    int accent_cv, octave_cv;

    // Animation
    int pulse_animation = 0;
    int reseed_animation = 0;
    int reset_animation = 0;
    int value_animation = 0;

    ProbLoopLinker &loop_linker = ProbLoopLinker::get();

    const int *weights_ptr[4] = {&weight_1, &weight_2, &weight_4, &weight_8};
    const int divs[4] = {1, 2, 4, 8};

    // === ProbDiv methods ===
    void FireMelody(int ch) {
        if (loop_linker.IsLooping()) {
            pitch[ch] = seqloop[ch][loop_linker.GetLoopStep()];
        } else {
            pitch[ch] = GetNextWeightedPitch();
        }

        // Accent: probabilistic velocity accent
        bool do_accent = false;
        if (accent_prob > 0) {
            int rnd = random(0, 16);
            do_accent = (rnd < accent_prob);
        }

        // Octave shift: probabilistic octave up/down
        int octave_shift = 0;
        if (octave_prob > 0) {
            int rnd = random(0, 16);
            if (rnd < octave_prob) {
                // Randomly up or down
                octave_shift = (random(0, 2) == 0) ? 12 : -12;
            }
        }

        int cv = pitch[ch] + (12 * OC::DAC::kOctaveZero) + octave_shift;
        Out(ch, cv);

        // Gate: high for accent, normal otherwise
        GateOut(ch, true);
        // Store accent info for MIDI velocity mapping later
        accent_cv = do_accent ? 127 : 64;
        octave_cv = octave_shift;
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
    static uint32_t get_non_neg_mask(T* arr, int n) {
        uint32_t mask = 0;
        for (int i = 0; i < n; ++i) {
            if (arr[i] >= 0) mask |= 1 << i;
        }
        return mask;
    }

    static int semitones_to_degrees(uint32_t scale_mask, int semitones) {
        semitones = ((semitones % 12) + 12) % 12;
        semitones -= __builtin_ctz(scale_mask);
        scale_mask >>= __builtin_ctz(scale_mask);
        int degrees = 0;
        while (semitones > 0 && scale_mask) {
            int rot = __builtin_ctz(scale_mask >> 1) + 1;
            semitones -= rot;
            scale_mask >>= rot;
            degrees++;
        }
        return scale_mask ? degrees : 0;
    }

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

    void UpdateRotatedWeights(int8_t* src, int8_t* rot, int semi_rot, int masked_rot) {
        std::copy(src, src + 12, rot);
        masked_rot -= semi_rot;
        uint32_t scale_mask = get_non_neg_mask(rot, 12);
        int degrees = semitones_to_degrees(scale_mask, masked_rot);
        rotate_masked_left(rot, scale_mask, 12, -degrees);
        rotate_masked_left(rot, 0xffff, 12, -semi_rot);
    }

    uint8_t GetNextWeightedPitch() {
        int total_weights = 0;
        int8_t rotated[12];
        UpdateRotatedWeights(weights, rotated, rotation[0], rotation[1]);
        for (int i = down_mod - 1; i < up_mod; ++i) {
            total_weights += max(0, rotated[i % 12]);
        }
        int rnd = random(0, total_weights + 1);
        for (int i = down_mod - 1; i < up_mod; ++i) {
            int w = max(0, rotated[i % 12]);
            if (rnd <= w && w > 0) return i;
            rnd -= w;
        }
        return 0;
    }

    void GenerateMeloLoop() {
        int full_seed = 0;
        for (int p = 0; p < 12; p++) {
            full_seed ^= (weights[p] + 1) << p;
        }
        full_seed ^= ((up_mod << 6) | down_mod);
        full_seed |= (loop_linker.GetSeed() << 16);
        randomSeed(full_seed);
        for (int i = 0; i < 32; ++i) {
            seqloop[0][i] = GetNextWeightedPitch();
            seqloop[1][i] = GetNextWeightedPitch();
        }
    }

    static constexpr uint8_t x_pos[12] = {2, 7, 10, 15, 18, 26, 31, 34, 39, 42, 47, 50};
    static constexpr uint8_t p_pos[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    static constexpr char n_pos[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};

    int semitone_cv_in(uint8_t cv_mask) {
        int out = 0;
        if (cv_mask & probmelod::CV1) out += SemitoneIn(0);
        if (cv_mask & probmelod::CV2) out += SemitoneIn(1);
        return out;
    }

    // === Display: side-by-side ===
    // Left half (x 0-63): ProbDiv
    // Right half (x 64-127): ProbMeloD

    void DrawDivSide() {
        // Division weights — left side
        for (int i = 0; i < 4; i++) {
            int x = 1;
            int y = 15 + (i * 10);
            gfxPrint(x, y, "/");
            gfxPrint(x + 6, y, divs[i]);
            DrawSlider(x + 14, y, 30, *weights_ptr[i], MAX_WEIGHT, cursor == i);
            if (pulse_animation > 0 && skip_steps == divs[i]) {
                gfxInvert(x, y, 12, 8);
            }
        }

        // Loop indicator
        gfxIcon(4, 55, LOOP_ICON);
        if (reseed_animation > 0) gfxInvert(4, 55, 12, 8);
        if (loop_length_mod == 0) {
            gfxPrint(19, 55, "off");
        } else {
            gfxPrint(19, 55, loop_length_mod);
        }
        if (cursor == LOOP_LENGTH) gfxCursor(19, 63, 18);

        if (reset_animation > 0) gfxPrint(52, 55, "R");

        // Div CV mode indicator
        if (cursor == DIV_CV_MODE) {
            gfxPos(1, 5);
            gfxPrint("CV:");
            gfxPrint(18, 5, div_cv_mode < DIV_CV_LAST ? "L" : "?");
        }
    }

    void DrawMeloSide() {
        int ox = 64;  // Right half offset

        // Draw note weights as vertical bars
        int8_t ws[12];
        if (MELO_ROTATE <= cursor && cursor <= MELO_ROTATE) {
            std::copy(weights, weights + 12, ws);
        } else {
            UpdateRotatedWeights(weights, ws, rotation[0], rotation[1]);
        }

        for (uint8_t i = 0; i < 12; ++i) {
            uint8_t xOff = ox + (i * 5);
            uint8_t yOff = 45;
            bool unmasked = (ws[i] >= 0);

            if (unmasked) {
                int bar_h = constrain(ws[i], 0, 10);
                gfxLine(xOff, yOff - bar_h, xOff + 2, yOff - bar_h);
            }
        }

        // Range indicators
        gfxPrint(ox + 1, 15, "L:");
        gfxPrint(ox + 15, 15, down_mod);
        if (cursor == MELO_LOWER) gfxCursor(ox + 15, 23, 12);

        gfxPrint(ox + 34, 15, "H:");
        gfxPrint(ox + 48, 15, up_mod);
        if (cursor == MELO_UPPER) gfxCursor(ox + 48, 23, 12);

        // CV mode
        if (cursor == MELO_CV_MODE) {
            gfxPos(ox + 1, 5);
            gfxPrint(probmelod::cv_modes[melo_cv_mode].cv1_label);
            gfxPos(ox + 32, 5);
            gfxPrint(probmelod::cv_modes[melo_cv_mode].cv2_label);
            gfxCursor(ox + 1, 13, 62, "CV");
        }

        // Accent/Octave probability
        gfxPrint(ox + 1, 55, "A:");
        gfxPrint(ox + 12, 55, accent_prob);
        if (cursor == ACCENT_PROB) gfxCursor(ox + 12, 63, 12);

        gfxPrint(ox + 34, 55, "O:");
        gfxPrint(ox + 45, 55, octave_prob);
        if (cursor == OCTAVE_PROB) gfxCursor(ox + 45, 63, 12);

        // Note indicators
        ForEachChannel(ch) {
            int note = pitch[ch] % 12;
            uint8_t xOff = ox + (note * 5) + 1;
            gfxIcon(xOff, 59, ch ? UP_TRI_R_HALF : UP_TRI_L_HALF);
        }
    }
};
