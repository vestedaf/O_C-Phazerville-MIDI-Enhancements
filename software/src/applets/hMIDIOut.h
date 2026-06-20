// Copyright (c) 2018, Jason Justian
// Copyright (c) 2025, Nicholas J. Michalek
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// See https://www.pjrc.com/teensy/td_midi.html

// The functions available for each output
#ifndef _HEM_H_MIDI_OUT_H_
#define _HEM_H_MIDI_OUT_H_
class hMIDIOut : public HemisphereApplet {
public:

    enum hMIDIOut_Cursor {
        MAP_INDEX_A,
        hMIDIOut_A_MIDI_CHANNEL,
        hMIDIOut_A_OUTPUT_MODE,
        hMIDIOut_A_DAC_SOURCE,
        hMIDIOut_A_GATE_SOURCE,
        MAP_A_RANGELOW, MAP_A_RANGEHIGH,

        MAP_INDEX_B,
        hMIDIOut_B_MIDI_CHANNEL,
        hMIDIOut_B_OUTPUT_MODE,
        hMIDIOut_B_DAC_SOURCE,
        hMIDIOut_B_GATE_SOURCE,
        MAP_B_RANGELOW, MAP_B_RANGEHIGH,

        hMIDIOut_LOG_VIEW,

        hMIDIOut_CURSOR_LAST = hMIDIOut_LOG_VIEW
    };

    const char* applet_name() {
        return "MIDIOut";
    }
    const uint8_t* applet_icon() { return PhzIcons::midiOut; }

    void Start() {
        map_index[0] = io_offset;
        map_index[1] = io_offset + 1;
        ForEachChannel(ch) {
            MIDIMapping &map = frame.MIDIState.outmap[map_index[ch]];
            map.Init();
        }
        frame.MIDIState.log_index = 0;
    }

    void Controller() {
        ForEachChannel(ch) {
            int midx = map_index[ch];
            MIDIMapping &map = frame.MIDIState.outmap[midx];
            if (!map.enabled()) continue;

            int dac_ch = map.get_voice();
            int cv = frame.ViewOut(dac_ch);
            uint8_t ch_num = map.get_channel();

            switch (map.get_type()) {
              case MIDIMapSettings::NONE:
                break;

              case MIDIMapSettings::PITCH: {
                // Note output with gate source (mirrors 1.xx SendFlexibleMIDIOut)
                const int8_t gs = map.get_gate_source();
                uint8_t note = MIDIQuantizer::NoteNumber(cv, map.get_transpose());
                uint8_t last = last_note[ch];

                int gate_cv = frame.ViewOut(gs);
                bool gate_now = gate_cv > (12 << 7);
                bool gate_was = last_gate[ch];

                if (gate_now && !gate_was) {
                  if (last != 0) frame.MIDIState.SendNoteOff(ch_num, last, 0);
                  frame.MIDIState.SendNoteOn(ch_num, note, 0x64);
                  UpdateLog(HEM_MIDI_NOTE_ON, note, 0x64);
                  last_note[ch] = note;
                  last_gate[ch] = true;
                  last_tick[ch] = OC::CORE::ticks;
                } else if (gate_now && gate_was && note != last) {
                  // Legato: pitch changed while gate held
                  frame.MIDIState.SendNoteOff(ch_num, last, 0);
                  frame.MIDIState.SendNoteOn(ch_num, note, 0x64);
                  UpdateLog(HEM_MIDI_NOTE_ON, note, 0x64);
                  last_note[ch] = note;
                  last_tick[ch] = OC::CORE::ticks;
                } else if (!gate_now && gate_was) {
                  frame.MIDIState.SendNoteOff(ch_num, last, 0);
                  UpdateLog(HEM_MIDI_NOTE_OFF, last, 0);
                  last_note[ch] = 0;
                  last_gate[ch] = false;
                  last_tick[ch] = OC::CORE::ticks;
                }
                break;
              }

              case MIDIMapSettings::DRUM: {
                // Gate output: note on/off based on gate threshold
                bool gate = (cv > HEMISPHERE_CHANGE_THRESHOLD * 3);
                if (gate && !last_gate[ch]) {
                    uint8_t note = MIDIQuantizer::NoteNumber(cv, map.get_transpose());
                    frame.MIDIState.SendNoteOn(ch_num, note, 0x64);
                    last_note[ch] = note;
                    UpdateLog(HEM_MIDI_NOTE_ON, note, 0x64);
                    last_tick[ch] = OC::CORE::ticks;
                } else if (!gate && last_gate[ch]) {
                    frame.MIDIState.SendNoteOff(ch_num, last_note[ch], 0);
                    UpdateLog(HEM_MIDI_NOTE_OFF, last_note[ch], 0);
                    last_tick[ch] = OC::CORE::ticks;
                }
                last_gate[ch] = gate;
                break;
              }

              case MIDIMapSettings::TRIGGER: {
                // Trigger: short note on gate rising edge
                bool gate = (cv > HEMISPHERE_CHANGE_THRESHOLD * 3);
                if (gate && !last_gate[ch]) {
                    uint8_t note = MIDIQuantizer::NoteNumber(cv, map.get_transpose());
                    frame.MIDIState.SendNoteOn(ch_num, note, 0x64);
                    last_note[ch] = note;
                    trig_countdown[ch] = 100;
                    UpdateLog(HEM_MIDI_NOTE_ON, note, 0x64);
                    last_tick[ch] = OC::CORE::ticks;
                }
                if (trig_countdown[ch] > 0) {
                    if (--trig_countdown[ch] == 0) {
                        frame.MIDIState.SendNoteOff(ch_num, last_note[ch], 0);
                        UpdateLog(HEM_MIDI_NOTE_OFF, last_note[ch], 0);
                    }
                }
                last_gate[ch] = gate;
                break;
              }

              case MIDIMapSettings::MODULATOR: {
                // Modulator: send velocity, aftertouch, pitch bend, or CC#
                int value = ProportionCV(cv, 127, HEMISPHERE_MAX_CV);
                if (map.get_subtype() <= MIDIMapSettings::MOD_BEND) {
                  switch (map.get_subtype()) {
                    case MIDIMapSettings::MOD_VEL_MONO:
                      // Velocity stored for note-on
                      last_velocity[ch] = value;
                      break;
                    case MIDIMapSettings::MOD_AT_CHAN:
                      if (value != last_at[ch]) {
                        frame.MIDIState.SendAfterTouch(ch_num, value);
                        last_at[ch] = value;
                        UpdateLog(HEM_MIDI_AFTERTOUCH_CHANNEL, value, 0);
                        last_tick[ch] = OC::CORE::ticks;
                      }
                      break;
                    case MIDIMapSettings::MOD_BEND: {
                      uint16_t bend = Proportion(cv + HEMISPHERE_3V_CV, HEMISPHERE_3V_CV * 2, 16383);
                      bend = constrain(bend, 0, 16383);
                      if (bend != last_bend[ch]) {
                        frame.MIDIState.SendPitchBend(ch_num, bend);
                        last_bend[ch] = bend;
                        UpdateLog(HEM_MIDI_PITCHBEND, bend - 8192, 0);
                        last_tick[ch] = OC::CORE::ticks;
                      }
                      break;
                    }
                    default: break;
                  }
                } else {
                  // CC# mode: subtype 5-132 = CC# 0-127
                  uint8_t cc_num = map.get_subtype() - (MIDIMapSettings::MOD_BEND + 1);
                  if (value != last_cc[ch]) {
                    frame.MIDIState.SendCC(ch_num, cc_num, value);
                    last_cc[ch] = value;
                    UpdateLog(HEM_MIDI_CC, cc_num, value);
                    last_tick[ch] = OC::CORE::ticks;
                  }
                }
                break;
              }

              case MIDIMapSettings::CCONTROL: {
                // Legacy CC output (in-maps only)
                int value = ProportionCV(cv, 127, HEMISPHERE_MAX_CV);
                uint8_t cc_num = map.get_subtype();
                if (value != last_cc[ch]) {
                    frame.MIDIState.SendCC(ch_num, cc_num, value);
                    last_cc[ch] = value;
                    UpdateLog(HEM_MIDI_CC, cc_num, value);
                    last_tick[ch] = OC::CORE::ticks;
                }
                break;
              }

              case MIDIMapSettings::PIPE:
                // PIPE processing is handled in IOFrame::Send(), not here
                // This applet just displays the mapping state
                break;

              default: break;
            }
        }
    }

    void View() {
        switch (cursor) {
            default:
            case hMIDIOut_A_MIDI_CHANNEL:
            case hMIDIOut_A_OUTPUT_MODE:
            case hMIDIOut_A_DAC_SOURCE:
            case hMIDIOut_A_GATE_SOURCE:
            case hMIDIOut_B_MIDI_CHANNEL:
            case hMIDIOut_B_OUTPUT_MODE:
            case hMIDIOut_B_DAC_SOURCE:
            case hMIDIOut_B_GATE_SOURCE:
            case MAP_A_RANGELOW:
            case MAP_A_RANGEHIGH:
            case MAP_B_RANGELOW:
            case MAP_B_RANGEHIGH:
                DrawChannelPage();
                DrawMonitor();
                break;
            case hMIDIOut_LOG_VIEW:
                DrawLog();
                break;
        }
    }

    void AuxButton() override {
      switch (cursor) {
        case hMIDIOut_A_OUTPUT_MODE:
        case hMIDIOut_B_OUTPUT_MODE:
          {
            int ch = map_index[io_page];
            MIDIMapping &map = frame.MIDIState.outmap[ch];
            map.Init();
            break;
          }
        default: break;
      }
      CancelEdit();
    }

    void OnButtonPress() {
        CursorToggle();
    }

    void OnEncoderMove(int direction) {
        if (!EditMode()) {
            MoveCursor(cursor, direction, hMIDIOut_CURSOR_LAST);
            io_page = (cursor >= MAP_INDEX_B);
            return;
        }
        int midx = map_index[io_page];
        MIDIMapping &map = frame.MIDIState.outmap[midx];
        switch (cursor) {
            case MAP_INDEX_A:
            case MAP_INDEX_B:
                map_index[cursor > MAP_INDEX_A] =
                  constrain(map_index[cursor > MAP_INDEX_A] + direction, 0, HS::MIDIMAP_MAX - 1);
                break;
            case hMIDIOut_A_MIDI_CHANNEL:
            case hMIDIOut_B_MIDI_CHANNEL:
                map.AdjustChannel(direction);
                break;
            case hMIDIOut_A_OUTPUT_MODE:
            case hMIDIOut_B_OUTPUT_MODE:
                map.AdjustFunctionOutput(direction);
                break;
            case hMIDIOut_A_DAC_SOURCE:
            case hMIDIOut_B_DAC_SOURCE:
                map.AdjustVoice(direction);
                break;
            case hMIDIOut_A_GATE_SOURCE:
            case hMIDIOut_B_GATE_SOURCE:
                map.AdjustGateSource(direction);
                break;
            case MAP_A_RANGELOW:
            case MAP_B_RANGELOW:
                map.AdjustRangeLow(direction);
                break;
            case MAP_A_RANGEHIGH:
            case MAP_B_RANGEHIGH:
                map.AdjustRangeHigh(direction);
                break;
            case hMIDIOut_LOG_VIEW:
            default:
                break;
        }
        ResetCursor();
    }

    uint64_t OnDataRequest() {
        uint64_t data = 0;
        Pack(data, PackLocation{0, 5}, map_index[0]);
        Pack(data, PackLocation{8, 5}, map_index[1]);
        return data;
    }

    void OnDataReceive(uint64_t data) {
        map_index[0] = Unpack(data, PackLocation {0,5});
        map_index[1] = Unpack(data, PackLocation {8,5});
    }

protected:
    void SetHelp() {
        //                      "-------" <-- Label size guide
        //help[HELP_DIGITAL1] = "";
        //help[HELP_DIGITAL2] = "";
        //help[HELP_CV1]      = "";
        //help[HELP_CV2]      = "";
        help[HELP_OUT1]       = frame.MIDIState.outmap[map_index[0]].get_out_label();
        help[HELP_OUT2]       = frame.MIDIState.outmap[map_index[1]].get_out_label();
        //help[HELP_EXTRA1]   = "";
        //help[HELP_EXTRA2]   = "";
        //                      "---------------------" <-- Extra text size guide
    }

private:
    // Housekeeping
    int cursor;
    int map_index[2] = {0, 1};
    int io_page = 0;
    int last_icon_ticks[2];

    // Per-channel state
    uint8_t last_note[2] = {0, 0};
    uint8_t last_velocity[2] = {0x64, 0x64};
    uint8_t last_cc[2] = {0, 0};
    uint8_t last_at[2] = {0, 0};
    uint16_t last_bend[2] = {8192, 8192};
    bool last_gate[2] = {false, false};
    uint32_t last_tick[2] = {0, 0};
    uint16_t trig_countdown[2] = {0, 0};

    // Logging
    MIDIMessage log[7];
    int log_index;

    void DrawMonitor() {
        if ((OC::CORE::ticks - frame.MIDIState.last_msg_tick) < 100) {
            if (frame.MIDIState.outmap[map_index[0]].get_channel() == frame.MIDIState.last_midi_channel)
                last_icon_ticks[0] = OC::CORE::ticks;
            if (frame.MIDIState.outmap[map_index[1]].get_channel() == frame.MIDIState.last_midi_channel)
                last_icon_ticks[1] = OC::CORE::ticks;
        }

        if (OC::CORE::ticks - last_icon_ticks[io_page] < 4000)
            gfxIcon(54, 13, MIDI_ICON);
    }

    void DrawChannelPage() {
        gfxPrint(1, 13, OutputLabel(io_page));
        graphics.printf(":  M%2d", map_index[io_page] + 1);

        // ------------------ //
        gfxLine(1, 22, 63, 22);

        MIDIMapping &map = frame.MIDIState.outmap[map_index[io_page]];
        uint8_t m_ch = map.get_channel();
        gfxPrint(1, 25, "MIDICh:");
        if (m_ch > 15) graphics.printf("%3s", "Om");
        else graphics.printf("%3d", m_ch + 1);

        gfxIcon(2, 34, PhzIcons::midiOut);
        gfxPrint(13, 35, map.get_out_label());

        if (cursor >= (MAP_A_RANGELOW + io_page*(MAP_A_RANGEHIGH+1))) {
          // Range edit mode: show range brackets
          gfxPrint(1, 45, "<"); gfxPrint(HS::midi_note_numbers[map.get_low()]);
          gfxPrint(34, 45, HS::midi_note_numbers[map.get_high()]); gfxPrint(">");
        } else {
          // Default view: show DAC source + type-specific info on one line
          if (map.get_type() == MIDIMapSettings::DRUM) {
            // DRUM: "G/T:M nn/Cn" — DAC source + MIDI note number + note name
            gfxPrint(1, 45, "G/T:");
            gfxPrint(19, 45, map.get_voice() + 1);
            graphics.setPrintPos(30, 45);
            uint8_t note = map.GetDrumNote();
            gfxPrint(note);
            gfxPrint("/");
            gfxPrint(HS::midi_note_numbers[note]);
          } else if (map.get_type() == MIDIMapSettings::PIPE) {
            // PIPE: "Map:M1" — shows IN-map source
            gfxPrint(1, 45, "Map:M");
            gfxPrint(map.get_voice() + 1); // 0→1, 1→2, ..., 31→32
          } else {
            // All other types: "DAC:X" + gate source for PITCH
            gfxPrint(1, 45, "DAC:");
            const char* outname = map.GetOutputName(map.get_voice());
            gfxPrint(22, 45, outname);
            if (map.get_type() == MIDIMapSettings::PITCH) {
              int gate_x = 22 + strlen(outname) * 6;
              gfxPrint(gate_x, 45, "G:");
              if (map.get_gate_source() >= 0)
                gfxPrint(gate_x + 12, 45, map.get_gate_source() + 1);
              else
                gfxPrint(gate_x + 12, 45, "CV");
            }
          }
        }

        // Cursor
        switch (cursor) {
            case MAP_INDEX_A:
            case MAP_INDEX_B:
                gfxCursor(19, 21, 19);
                break;
            case hMIDIOut_A_MIDI_CHANNEL:
            case hMIDIOut_B_MIDI_CHANNEL:
                gfxCursor(42, 33, 21);
                break;
            case hMIDIOut_A_OUTPUT_MODE:
            case hMIDIOut_B_OUTPUT_MODE:
                gfxCursor(12, 43, 51);
                break;
            case hMIDIOut_A_DAC_SOURCE:
            case hMIDIOut_B_DAC_SOURCE:
                if (map.get_type() == MIDIMapSettings::DRUM)
                  gfxCursor(25, 53, 10); // drum: cursor over voice number (after "G/T:")
                else
                  gfxCursor(25, 53, 10); // DAC value (after "DAC:")
                break;
            case hMIDIOut_A_GATE_SOURCE:
            case hMIDIOut_B_GATE_SOURCE: {
                // Position cursor after "DAC:X G:" where X is the output name
                const char* outname = map.GetOutputName(map.get_voice());
                int gate_cursor_x = 22 + strlen(outname) * 6 + 12; // after "DAC:" + name + " G:"
                gfxCursor(gate_cursor_x, 53, 10);
                break;
              }
            case MAP_A_RANGELOW:
            case MAP_B_RANGELOW:
                gfxCursor(7, 53, 19);
                break;
            case MAP_A_RANGEHIGH:
            case MAP_B_RANGEHIGH:
                gfxCursor(34, 53, 19);
                break;
            default: break;
        }

        // Last log entry
        if (frame.MIDIState.log_index > 0) {
            PrintLogEntry(56, frame.MIDIState.log_index - 1);
        }
        gfxInvert(0, 55, 63, 9);
    }

    void DrawLog() {
        if (frame.MIDIState.log_index) {
            for (int i = 0; i < frame.MIDIState.log_index; i++) {
                PrintLogEntry(15 + i * 8, i);
            }
        }
    }

    void PrintLogEntry(int y, int index) {
        MIDILogEntry &log_entry_ = frame.MIDIState.log[index];

        switch (log_entry_.message) {
            case HEM_MIDI_NOTE_ON:
                gfxIcon(1, y, NOTE_ICON);
                gfxPrint(10, y, midi_note_numbers[log_entry_.data1]);
                gfxPrint(40, y, log_entry_.data2);
                break;

            case HEM_MIDI_NOTE_OFF:
                gfxPrint(1, y, "-");
                gfxPrint(10, y, midi_note_numbers[log_entry_.data1]);
                break;

            case HEM_MIDI_CC:
                gfxIcon(1, y, MOD_ICON);
                gfxPrint(10, y, log_entry_.data2);
                break;

            case HEM_MIDI_AFTERTOUCH_CHANNEL:
                gfxIcon(1, y, AFTERTOUCH_ICON);
                gfxPrint(10, y, log_entry_.data1);
                break;

            case HEM_MIDI_AFTERTOUCH_POLY:
                gfxIcon(1, y, AFTERTOUCH_ICON);
                gfxPrint(10, y, log_entry_.data2);
                break;

            case HEM_MIDI_PITCHBEND: {
                int data = (log_entry_.data2 << 7) + log_entry_.data1 - 8192;
                gfxIcon(1, y, BEND_ICON);
                gfxPrint(10, y, data);
                break;
                }

            default:
                gfxPrint(1, y, "?");
                gfxPrint(10, y, log_entry_.data1);
                gfxPrint(" ");
                gfxPrint(log_entry_.data2);
                break;
        }
    }

    void UpdateLog(uint8_t msg, uint8_t data1, uint8_t data2) {
        if (log_index < 7) {
            log[log_index].message = msg;
            log[log_index].data1 = data1;
            log[log_index].data2 = data2;
            log_index++;
        }
    }
};

#endif
