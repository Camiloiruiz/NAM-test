#include "NamForceLink.h"

#include "NAM/model_config.h"
#include "NAM/convnet.h"
#include "NAM/lstm.h"
#include "NAM/wavenet.h"

void RegisterNamArchitectures()
{
    auto& reg = nam::ConfigParserRegistry::instance();

    if (!reg.has("ConvNet"))
        reg.registerParser("ConvNet", nam::convnet::create_config);

    if (!reg.has("LSTM"))
        reg.registerParser("LSTM", nam::lstm::create_config);

    if (!reg.has("WaveNet"))
        reg.registerParser("WaveNet", nam::wavenet::create_config);
}
