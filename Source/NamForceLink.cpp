// NamForceLink.cpp
//
// Forces the linker to include every NAM architecture translation unit so
// their file-scope static initialisers (which self-register into
// ConfigParserRegistry) are guaranteed to run — even when LTO is active.
//
// Taking the address of each create_config function creates a real symbol
// reference that the linker cannot legally discard.

#include "NAM/convnet.h"
#include "NAM/lstm.h"
#include "NAM/wavenet.h"

namespace
{
// volatile prevents the compiler from discarding these as dead stores.
volatile auto* _keep_convnet = &nam::convnet::create_config;
volatile auto* _keep_lstm    = &nam::lstm::create_config;
volatile auto* _keep_wavenet = &nam::wavenet::create_config;
} // namespace
