#pragma once

// Explicitly registers all built-in NAM architectures (WaveNet, LSTM, ConvNet)
// into ConfigParserRegistry. Call this once before loading any .nam model.
// Safe to call multiple times — duplicate registrations are skipped.
void RegisterNamArchitectures();
