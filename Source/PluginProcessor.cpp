#include "PluginProcessor.h"

namespace
{
    // Lanes: 0 Kick, 1 Snare, 2 Clap, 3 Closed Hat, 4 Open Hat, 5 Perc
    // 16 steps per bar. 1.0 = core hit (always plays), lower = chance of a hit
    // (scaled by the Variation knob).
    const char* laneNames[6]   = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Perc" };
    const int   defaultNotes[6] = { 36, 38, 39, 42, 46, 37 };   // General MIDI drum map
    const int   laneBaseVel[6]  = { 112, 105, 100, 78, 90, 82 };

    const float templates[3][6][16] =
    {
        // ---------------------------------------------------------------- HOUSE
        {
            /* kick   */ { 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0 },
            /* snare  */ { 0,0,0,0, 0,0,0,.08f, 0,0,0,.05f, 0,0,.1f,0 },
            /* clap   */ { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 },
            /* closed */ { .3f,.5f,0,.5f, .3f,.5f,0,.5f, .3f,.5f,0,.5f, .3f,.5f,0,.5f },
            /* open   */ { 0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0 },
            /* perc   */ { 0,0,0,.12f, 0,.1f,0,.18f, 0,.12f,0,.1f, .15f,0,.15f,.1f }
        },
        // --------------------------------------------------------------- TECHNO
        {
            /* kick   */ { 1,0,0,.04f, 1,0,0,0, 1,0,0,.05f, 1,0,0,0 },
            /* snare  */ { 0,0,0,0, .35f,0,0,0, 0,0,0,.1f, .35f,0,.15f,0 },
            /* clap   */ { 0,0,0,0, 1,0,0,0, 0,0,.1f,0, 1,0,0,.12f },
            /* closed */ { .9f,.55f,.15f,.55f, .9f,.55f,.15f,.55f, .9f,.55f,.15f,.55f, .9f,.55f,.15f,.55f },
            /* open   */ { 0,0,1,0, 0,0,.6f,0, 0,0,1,0, 0,0,.6f,0 },
            /* perc   */ { 0,.2f,.3f,.45f, 0,.25f,.4f,.2f, 0,.2f,.3f,.4f, 0,.3f,.45f,.25f }
        },
        // ------------------------------------------------------------ BREAKBEAT
        {
            /* kick   */ { 1,0,0,.2f, 0,0,.35f,0, 0,.5f,1,0, 0,0,.2f,0 },
            /* snare  */ { 0,0,0,.12f, 1,0,.15f,.1f, 0,.25f,0,.1f, 1,0,.15f,.3f },
            /* clap   */ { 0,0,0,0, .25f,0,0,0, 0,0,0,0, .25f,0,0,0 },
            /* closed */ { .85f,.25f,.85f,.25f, .85f,.25f,.85f,.25f, .85f,.25f,.85f,.25f, .85f,.25f,.85f,.25f },
            /* open   */ { 0,0,0,0, 0,0,.2f,0, 0,0,0,0, 0,0,.2f,0 },
            /* perc   */ { 0,.1f,0,.2f, 0,.15f,.25f,0, 0,.2f,0,.15f, 0,.1f,.3f,.4f }
        }
    };

    // Stateless hash so the same seed always yields the same pattern.
    uint64_t mix (uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    int64_t floorDiv (int64_t a, int64_t b)
    {
        int64_t q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DrumRandomizerProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto fl = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi); };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style",
        StringArray { "House", "Techno", "Breakbeat" }, 0));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "bars", 1 }, "Length (Bars)", 1, 4, 2));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "variation", 1 }, "Variation", fl (0.f, 1.f), 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "fill", 1 }, "Fill Amount", fl (0.f, 1.f), 0.25f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", fl (0.f, 0.5f), 0.f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize Velocity", fl (0.f, 1.f), 0.4f));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "seed", 1 }, "Seed", 0, 9999, 1));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "evolve", 1 }, "New Pattern Each Loop", false));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "channel", 1 }, "MIDI Channel", 1, 16, 10));

    for (int i = 0; i < numLanes; ++i)
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "on" + String (i), 1 },
                                                           String (laneNames[i]) + " On", true));
    for (int i = 0; i < numLanes; ++i)
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "note" + String (i), 1 },
                                                          String (laneNames[i]) + " Note", 0, 127, defaultNotes[i]));

    return { std::make_move_iterator (p.begin()), std::make_move_iterator (p.end()) };
}

//==============================================================================
DrumRandomizerProcessor::DrumRandomizerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pStyle     = apvts.getRawParameterValue ("style");
    pBars      = apvts.getRawParameterValue ("bars");
    pVariation = apvts.getRawParameterValue ("variation");
    pFill      = apvts.getRawParameterValue ("fill");
    pSwing     = apvts.getRawParameterValue ("swing");
    pHumanize  = apvts.getRawParameterValue ("humanize");
    pSeed      = apvts.getRawParameterValue ("seed");
    pEvolve    = apvts.getRawParameterValue ("evolve");
    pChannel   = apvts.getRawParameterValue ("channel");

    for (int i = 0; i < numLanes; ++i)
    {
        pOn[(size_t) i]   = apvts.getRawParameterValue ("on" + juce::String (i));
        pNote[(size_t) i] = apvts.getRawParameterValue ("note" + juce::String (i));
    }
}

void DrumRandomizerProcessor::prepareToPlay (double, int)
{
    laneOn.fill (false);
    lastEndPpq = -1.0;
}

bool DrumRandomizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void DrumRandomizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midi.clear();

    const int numSamples = buffer.getNumSamples();

    auto killLane = [&] (size_t l, int offset)
    {
        if (laneOn[l])
        {
            midi.addEvent (juce::MidiMessage::noteOff (laneChannel[l], laneNote[l]), offset);
            laneOn[l] = false;
        }
    };
    auto killAll = [&] (int offset)
    {
        for (size_t l = 0; l < (size_t) numLanes; ++l) killLane (l, offset);
    };

    // --- transport -----------------------------------------------------------
    bool playing = false;
    double startPpq = 0.0, bpm = 120.0;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto pp = pos->getPpqPosition()) startPpq = *pp; else playing = false;
            bpm = pos->getBpm().orFallback (120.0);
        }

    if (! playing)
    {
        killAll (0);
        lastEndPpq = -1.0;
        return;
    }

    if (bpm <= 0.0) bpm = 120.0;

    const double samplesPerPpq = getSampleRate() * 60.0 / bpm;
    const double endPpq = startPpq + numSamples / samplesPerPpq;

    if (lastEndPpq >= 0.0 && startPpq < lastEndPpq - 1e-3)   // loop or jump back
        killAll (0);
    lastEndPpq = endPpq;

    auto flushOffs = [&]
    {
        for (size_t l = 0; l < (size_t) numLanes; ++l)
            if (laneOn[l] && laneOffPpq[l] < endPpq)
                killLane (l, juce::jlimit (0, numSamples - 1, (int) ((laneOffPpq[l] - startPpq) * samplesPerPpq)));
    };
    flushOffs();

    // --- parameters ----------------------------------------------------------
    const double L          = 0.25;                       // one 16th note, in quarter notes
    const int    style      = juce::jlimit (0, 2, (int) pStyle->load());
    const int    bars       = juce::jlimit (1, 4, (int) pBars->load());
    const int    total      = bars * 16;
    const float  variation  = pVariation->load();
    const float  fill       = pFill->load();
    const double swing      = pSwing->load();
    const float  humRange   = 14.0f * pHumanize->load();
    const uint64_t seed     = (uint64_t) pSeed->load();
    const bool   evolve     = pEvolve->load() > 0.5f;
    const int    channel    = juce::jlimit (1, 16, (int) pChannel->load());

    auto stepPos = [&] (int64_t k)
    {
        return (double) k * L + ((k & 1) ? swing * L * 0.5 : 0.0);
    };

    // --- step events in this block --------------------------------------------
    for (int64_t k = (int64_t) std::floor (startPpq / L) - 1; ; ++k)
    {
        const double pos = stepPos (k);
        if (pos >= endPpq) break;
        if (pos < startPpq) continue;

        const int offset = juce::jlimit (0, numSamples - 1, (int) ((pos - startPpq) * samplesPerPpq));
        const int64_t idxLoop = ((k % total) + total) % total;
        const int64_t cycle = evolve ? floorDiv (k, total) : 0;
        const int stepInBar = (int) (idxLoop % 16);
        const bool inFill = idxLoop >= total - 4;

        for (size_t l = 0; l < (size_t) numLanes; ++l)
        {
            if (pOn[l]->load() < 0.5f) continue;

            const float t = templates[style][l][stepInBar];
            float prob = (t >= 0.99f) ? 1.0f : juce::jmin (1.0f, t * variation * 2.0f);

            if (fill > 0.0f && inFill && (l == 1 || l == 5))   // fills on snare + perc
                prob = juce::jmax (prob, fill * 0.9f);

            juce::Random r ((juce::int64) mix (mix (mix (mix (seed) + (uint64_t) cycle) + (uint64_t) idxLoop) + (uint64_t) l));
            const float hitRoll = r.nextFloat();
            const float humRoll = r.nextFloat() * 2.0f - 1.0f;

            if (hitRoll >= prob) continue;

            int vel = laneBaseVel[l];
            if (stepInBar % 4 == 0)      vel += 8;
            else if (stepInBar % 2 == 1) vel -= 8;
            vel = juce::jlimit (1, 127, vel + (int) (humRoll * humRange));

            const int note = juce::jlimit (0, 127, (int) pNote[l]->load());

            killLane (l, offset);                          // retrigger
            if (l == 3) killLane (4, offset);              // closed hat chokes open hat

            midi.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) vel), offset);
            laneOn[l] = true;
            laneNote[l] = note;
            laneChannel[l] = channel;

            const double dur = (l == 4) ? 0.95 : 0.45;
            laneOffPpq[l] = juce::jmin (pos + dur * L, stepPos (k + 1) - 1e-4);
        }

        flushOffs();
    }
}

//==============================================================================
class DrumRandomizerEditor : public juce::AudioProcessorEditor
{
public:
    explicit DrumRandomizerEditor (DrumRandomizerProcessor& p)
        : AudioProcessorEditor (&p), proc (p), generic (p)
    {
        addAndMakeVisible (generic);
        addAndMakeVisible (randomButton);
        randomButton.setButtonText ("Randomize pattern");
        randomButton.onClick = [this]
        {
            if (auto* seed = proc.apvts.getParameter ("seed"))
            {
                seed->beginChangeGesture();
                seed->setValueNotifyingHost (juce::Random::getSystemRandom().nextFloat());
                seed->endChangeGesture();
            }
        };
        setSize (420, 720);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        randomButton.setBounds (b.removeFromTop (44).reduced (6));
        generic.setBounds (b);
    }

private:
    DrumRandomizerProcessor& proc;
    juce::GenericAudioProcessorEditor generic;
    juce::TextButton randomButton;
};

juce::AudioProcessorEditor* DrumRandomizerProcessor::createEditor()
{
    return new DrumRandomizerEditor (*this);
}

//==============================================================================
void DrumRandomizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumRandomizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrumRandomizerProcessor();
}
