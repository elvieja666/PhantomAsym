#include "PluginProcessor.h"

PhantomAsymProcessor::PhantomAsymProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PhantomAsymProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "xover", 1 }, "Sub Crossover",
        NormalisableRange<float> (30.0f, 200.0f, 0.1f, 0.5f), 90.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Sensitivity",
        NormalisableRange<float> (-60.0f, -6.0f, 0.1f), -30.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "smooth", 1 }, "Smooth",
        NormalisableRange<float> (0.1f, 20.0f, 0.01f, 0.5f), 1.5f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode",
        StringArray { "1 cycle yes / 1 no", "Half-wave (every cycle)" }, 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "first", 1 }, "First Cycle",
        StringArray { "Silence", "Pass" }, 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "polarity", 1 }, "Polarity",
        StringArray { "Positive", "Negative" }, 0));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-12.0f, 6.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    return { params.begin(), params.end() };
}

void PhantomAsymProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    crossover.prepare (spec);
    crossover.reset();
    controlState = 0.0f;
    cycleOn = false;
    nextDuck = true;
    wasPositive = false;
    quietCount = 0;
}

bool PhantomAsymProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void PhantomAsymProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const float xoverHz  = apvts.getRawParameterValue ("xover")->load();
    const float depth    = apvts.getRawParameterValue ("depth")->load() / 100.0f;
    const float thrGain  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("threshold")->load());
    const float smoothMs = apvts.getRawParameterValue ("smooth")->load();
    const float polarity = apvts.getRawParameterValue ("polarity")->load() < 0.5f ? 1.0f : -1.0f;
    const int   mode     = (int) apvts.getRawParameterValue ("mode")->load();
    const bool  firstDuck = apvts.getRawParameterValue ("first")->load() < 0.5f;
    const int   resetSamples = (int) (0.06 * currentSampleRate);
    const float outGain  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());

    crossover.setCutoffFrequency (xoverHz);

    const float coef = std::exp (-1.0f / (smoothMs * 0.001f * static_cast<float> (currentSampleRate)));

    float* chData[2] { nullptr, nullptr };
    for (int ch = 0; ch < numCh; ++ch)
        chData[ch] = buffer.getWritePointer (ch);

    for (int s = 0; s < numSamples; ++s)
    {
        float low[2] { 0.0f, 0.0f }, high[2] { 0.0f, 0.0f };
        float lowSum = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            crossover.processSample (ch, chData[ch][s], low[ch], high[ch]);
            lowSum += low[ch];
        }

        // El sub (mono) con su signo
        const float subSigned = polarity * (lowSum / (float) numCh);
        float target = 0.0f;

        if (mode == 0)
        {
            // 1 ciclo entero silenciado, el siguiente pasa, y asi alternando.
            // Un ciclo = del cruce ascendente al siguiente cruce ascendente del sub.
            const float hys = thrGain;

            if (! wasPositive && subSigned > hys)
            {
                wasPositive = true;
                cycleOn  = nextDuck;
                nextDuck = ! nextDuck;
            }
            else if (wasPositive && subSigned < -hys)
            {
                wasPositive = false;
            }

            // Si el sub se apaga (entre golpes de bombo), se reinicia el conteo
            // para que cada golpe arranque con el mismo patron.
            if (std::abs (subSigned) < hys) ++quietCount; else quietCount = 0;
            if (quietCount > resetSamples)
            {
                cycleOn = false;
                nextDuck = firstDuck;
                wasPositive = false;
                quietCount = resetSamples;
            }

            target = cycleOn ? 1.0f : 0.0f;
        }
        else
        {
            // Modo anterior: una bajada en un solo semiciclo de cada ciclo
            const float halfWave = juce::jmax (0.0f, subSigned);
            target = juce::jlimit (0.0f, 1.0f, halfWave / thrGain);
        }

        controlState = target + coef * (controlState - target);

        // Ganancia: solo atenua (maximo 1.0), nunca amplifica -> el headroom no sube.
        const float duck = 1.0f - depth * controlState;

        for (int ch = 0; ch < numCh; ++ch)
            chData[ch][s] = (low[ch] + high[ch] * duck) * outGain;
    }
}

juce::AudioProcessorEditor* PhantomAsymProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void PhantomAsymProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PhantomAsymProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhantomAsymProcessor();
}
