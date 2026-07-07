# DRUM to GATE Terminology Migration Plan

## Scope
Replace all DRUM terminology with GATE terminology in the MIDI enhancement codebase while preserving exact functionality.

## Key Changes Needed

### 1. HSIOFrame.h
- Line 136: `case DRUM: return 1;` → `case GATE: return 1;`
- Line 176: `static constexpr int DRUM_NOTE_COUNT = sizeof(drum_note_table);` → `static constexpr int GATE_NOTE_COUNT = sizeof(drum_note_table);`
- Line 275: `case DRUM:` → `case GATE:`
- Line 346: `function = DRUM;` → `function = GATE;`
- Line 350: `function_cc = constrain(index, 0, DRUM_NOTE_COUNT - 1);` → `function_cc = constrain(index, 0, GATE_NOTE_COUNT - 1);`
- Line 354: `return (get_type() == DRUM) ? drum_note_table[constrain(get_subtype(), 0, DRUM_NOTE_COUNT - 1)] : 0;` → `return (get_type() == GATE) ? drum_note_table[constrain(get_subtype(), 0, GATE_NOTE_COUNT - 1)] : 0;`
- Line 379: `return get_type() == DRUM;` → `return get_type() == GATE;`
- Line 382: `return get_type() == DRUM; // DRUM is the output-side equivalent of GATE` → `return get_type() == GATE; // GATE is the output-side equivalent of PITCH`
- Line 436: `NONE, PITCH, DRUM, TRIGGER, MODULATOR, CCONTROL, PIPE` → `NONE, PITCH, GATE, TRIGGER, MODULATOR, CCONTROL, PIPE`
- Line 439: `// Available: NONE, PITCH (Note), DRUM, PIPE (Map)` → `// Available: NONE, PITCH (Note), GATE, PIPE (Map)`
- Line 441: `NONE, PITCH, DRUM, PIPE` → `NONE, PITCH, GATE, PIPE`
- Line 449: `case DRUM: return 0;` → `case GATE: return 0;`
- Line 493: `// DRUM: position 2 should change type, not browse drum notes` → `// GATE: position 2 should change type, not browse drum notes`
- Line 494: `// output_max_subtype(DRUM) == 0, so any dir overflows → AdjustOutputType` → `// output_max_subtype(GATE) == 0, so any dir overflows → AdjustOutputType`
- Line 495: `if (get_type() == DRUM) {` → `if (get_type() == GATE) {`

### 2. hMIDIOut.h
- Line 114: `case MIDIMapSettings::DRUM: {` → `case MIDIMapSettings::GATE: {`
- Line 391: `if (map.get_type() == MIDIMapSettings::DRUM) {` → `if (map.get_type() == MIDIMapSettings::GATE) {`

### 3. DrumMap.h
- Line 23: `#ifdef DRUMMAP_GRIDS2` → `#ifdef GATEMAP_GRIDS2`
- Line 29: `#define HEM_DRUMMAP_PULSE_ANIMATION_TICKS 1000` → `#define GATEMAP_PULSE_ANIMATION_TICKS 1000`
- Line 30: `#define HEM_DRUMMAP_VALUE_ANIMATION_TICKS 16000` → `#define GATEMAP_VALUE_ANIMATION_TICKS 16000`
- Line 31: `#define HEM_DRUMMAP_AUTO_RESET_TICKS 30000` → `#define GATEMAP_AUTO_RESET_TICKS 30000`

## Verification Steps
1. Replace all DRUM references with GATE references
2. Update all related constants and comments
3. Verify build succeeds
4. Test functionality to ensure GATE behaves identically to DRUM
5. Confirm all T40, T41, T41_audio, T41_MTP builds successfully

## Critical Notes
- DRUM is functionally equivalent to GATE in output context (line 382 in HSIOFrame.h states "DRUM is the output-side equivalent of GATE")
- The drum_note_table remains unchanged - only the terminology changes
- All existing drum note values (36-51) remain valid
- The GATE terminology applies to the same functionality in the same contexts