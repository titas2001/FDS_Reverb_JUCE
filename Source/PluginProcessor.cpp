/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FDS_ReverbAudioProcessor::FDS_ReverbAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{

    Nx = Ny = Nz = 7;

    pStates.reserve(3); // prevents allocation errors
    

    for (int i = 0; i < 3; ++i)
        pStates.push_back(std::vector<double>(Nx, 0));

    p.resize(3);
    
    for (int i = 0; i < p.size(); ++i)
        p[i] = &pStates[i][0];


}

FDS_ReverbAudioProcessor::~FDS_ReverbAudioProcessor()
{
}

//==============================================================================
const juce::String FDS_ReverbAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FDS_ReverbAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FDS_ReverbAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FDS_ReverbAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double FDS_ReverbAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FDS_ReverbAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int FDS_ReverbAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FDS_ReverbAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String FDS_ReverbAudioProcessor::getProgramName (int index)
{
    return {};
}

void FDS_ReverbAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void FDS_ReverbAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    c = 346;        // speed of sound in air 
    rho = 1.168;    // air density
    v = 3200;       // speed of sound in concrete 
    rhoC = 2400;    // concrete density
    
    
    Z = rhoC * v;
    xi = Z / (rho * c);
    R = (xi - 1) / (xi + 1);

    Dg1 = 1 / 4;
    Dg2 = 1;
    Di1 = (R + 1) / (2 * (R + 3));
    Di2 = (3 * R + 1) / (R + 3);
    De1 = (R + 1) / (8);
    De2 = R;
    Dc1 = (R + 1) / (2 * (5 - R));
    Dc2 = (5 * R - 1) / (5 - R);
}

void FDS_ReverbAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FDS_ReverbAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void FDS_ReverbAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            vin = buffer.getSample(channel, sample);
            p[1][3 + 3*Ny + 3*Ny*Nz] += vin;
            calculateScheme();
            updateStates();
            vout = p[1][3 + 3*Ny + 3 *Ny*Nz];
            buffer.setSample(channel, sample, vout);
        }

    }
}
void FDS_ReverbAudioProcessor::calculateScheme()


/*

             H +--------+ G
              /        /|
             /        / |
          E +--------+ F|
            |        |  |
            |   D    |  + C
            |        | /
            |        |/
            +--------+
            A        B

*/


{
    /* ================================================= GENERAL ======================================================================*/

    for (int i = 1; i < Nx-1; ++i)            // not including the boundaries
    {
        for (int j = 1; j < Ny-1; ++j)        // not including the boundaries
        {
            for (int k = 1; k < Nz-1; ++k)    // not including the boundaries
            {
                p[0][i + (j)*Ny + (k)*Ny*Nz] =
                    Dg1 * (p[1][(i + 1) + (j)*Ny + (k)*Ny*Nz] + p[1][(i - 1) + (j)*Ny + (k)*Ny*Nz] + p[1][i + (j + 1) * Ny + (k)*Ny*Nz]
                        + p[1][i + (j - 1) * Ny + (k)*Ny*Nz] + p[1][i + (j)*Ny + (k + 1) * Ny*Nz] + p[1][i + (j)*Ny + (k - 1)*Ny*Nz]
                        + 2*p[1][i + (j)*Ny + (k)*Ny*Nz]) - Dg2 * p[2][i + (j)*Ny + (k)*Ny*Nz];
            }
        }
    }
    /*=================================================== INTERIOR =========================================================================*/
    for (int i = 1; i < Nx - 1; ++i) // Top and bottom faces
    {
        for (int j = 1; j < Ny - 1; ++j)
        {
            p[0][i + (j)*Ny + 0] =                                                                                              // Bottom ABCD
                Di1 * (p[1][(i + 1) + (j)*Ny] + p[1][(i - 1) + (j)*Ny] + p[1][i + (j + 1)*Ny] + p[1][i + (j - 1)*Ny] + p[1][i + (j)*Ny + (1)*Ny*Nz]
                    + p[1][i + (j)*Ny + (1)*Ny*Nz] + 2*p[1][i + (j)*Ny]) - Di2 * p[2][i + (j)*Ny];
            p[0][i + (j)*Ny + (Nz - 1)*Ny*Nz] =                                                                                             // Top EFGH
                Di1 * (p[1][(i + 1) + (j)*Ny + (Nz - 1)*Ny*Nz] + p[1][(i - 1) + (j)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (j + 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (j - 1) * Ny + (Nz - 1)*Ny*Nz] + p[1][i + (j)*Ny + (Nz - 1)*Ny*Nz]
                    + p[1][i + (j)*Ny + (Nz - 1)*Ny*Nz] + 2*p[1][i + (j)*Ny + (Nz - 1)*Ny*Nz]) - Di2 * p[2][i + (j)*Ny + (Nz - 1)*Ny*Nz];
        }
    }
    for (int i = 1; i < Nx - 1; ++i) // Front and Back faces
    {
        for (int k = 1; k < Nz - 1; ++k)
        {
            p[0][i + (k)*Ny*Nz] =                                                                                              // Front AFEF
            Di1 * (p[1][(i + 1) + (k)*Ny*Nz] + p[1][(i - 1) + (k)*Ny*Nz] + p[1][i + (1)*Ny + (k)*Ny*Nz] + p[1][i + (1)*Ny + (k)*Ny*Nz] + p[1][i + (k + 1)*Ny*Nz]
                + p[1][i + (k - 1)*Ny*Nz] + 2*p[1][i + (k)*Ny*Nz]) - Di2 * p[2][i + (k)*Ny*Nz];
            p[0][i + (Ny - 1)*Ny + (k)*Ny*Nz] =                                                                                             // Back CDGH
                Di1 * (p[1][(i + 1) + (Ny - 1)*Ny + (k)*Ny*Nz] + p[1][(i - 1) + (Ny - 1)*Ny + (k)*Ny*Nz] + p[1][i + (Ny - 1 - 1)*Ny + (k)*Ny*Nz] + p[1][i + (Ny - 1 - 1)*Ny + (k)*Ny*Nz] + p[1][i + (Ny)*Ny + (k + 1)*Ny*Nz]
                    + p[1][i + (Ny - 1)*Ny + (k - 1)*Ny*Nz] + 2*p[1][i + (Ny - 1)*Ny + (k)*Ny*Nz]) - Di2 * p[2][i + (Ny - 1)*Ny + (k)*Ny*Nz];
        }
    }
    for (int j = 1; j < Ny - 1; ++j) // Left and Right faces
    {
        for (int k = 1; k < Nz - 1; ++k)
        {
            p[0][(j)*Ny + (k)*Ny*Nz] =                                                                                              // Left ADEH
                Di1 * (p[1][(1) + (j)*Ny + (k)*Ny*Nz] + p[1][(1) + (j)*Ny + (k)*Ny*Nz] + p[1][(j + 1)*Ny + (k)*Ny*Nz] + p[1][(j - 1)*Ny + (k)*Ny*Nz] + p[1][(j)*Ny + (k + 1)*Ny*Nz]
                    + p[1][(j)*Ny + (k - 1)*Ny*Nz] + 2*p[1][(j)*Ny + (k)*Ny*Nz]) - Di2 * p[2][(j)*Ny + (k)*Ny*Nz];
            p[0][(Nx - 1) + (j)*Ny + (k)*Ny*Nz] =                                                                                             // Right BCFG
                Di1 * (p[1][(Nx - 1 - 1) + (j)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1 - 1) + (j)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (j + 1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (j - 1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (j)*Ny + (k + 1)*Ny*Nz]
                    + p[1][(Nx - 1) + (j)*Ny + (k - 1)*Ny*Nz] + 2*p[1][(Nx - 1) + (j)*Ny + (k)*Ny*Nz]) - Di2 * p[2][(Nx - 1) + (j)*Ny + (k)*Ny*Nz];
        }
    }
    /*=================================================== EDGES =========================================================================*/

    for (int i = 1; i < Nx - 1; ++i) // X edges
    {
        p[0][i] =                                                                                              // AB
            De1 * (p[1][(i + 1)] + p[1][(i - 1)] + p[1][i + (1)*Ny] + p[1][i + (1)*Ny] + p[1][i + (1)*Ny*Nz]
                + p[1][i + (1)*Ny*Nz] + 2*p[1][i]) - De2 * p[2][i];
        p[0][i + (Ny - 1)*Ny] =                                                                                             // CD
            De1 * (p[1][(i + 1) + (Ny - 1)*Ny] + p[1][(i - 1) + (Ny - 1)*Ny] + p[1][i + (Ny - 1 - 1)*Ny] + p[1][i + (Ny - 1 - 1)*Ny] + p[1][i + (Ny - 1)*Ny + (1)*Ny*Nz]
                + p[1][i + (Ny - 1)*Ny + (1)*Ny*Nz] + 2*p[1][i + (Ny - 1)*Ny]) - De2 * p[2][i + (Ny - 1)*Ny];
        p[0][i + (Nz - 1)*Ny*Nz] =                                                                                             // EF
            De1 * (p[1][(i + 1) + (Nz - 1)*Ny*Nz] + p[1][(i - 1) + (Nz - 1)*Ny*Nz] + p[1][i + (1)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (1)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (Nz - 1 - 1)*Ny*Nz]
                + p[1][i + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][i + (Nz - 1)*Ny*Nz]) - De2 * p[2][i + (Nz - 1)*Ny*Nz];
        p[0][i + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] =                                                                                            // GH
            De1 * (p[1][(i + 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(i - 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (Ny - 1 - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (Ny - 1 - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][i + (Ny - 1)*Ny + (Nz - 1 - 1)*Ny*Nz]
                + p[1][i + (Ny - 1)*Ny + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][i + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz]) - De2 * p[2][i + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz];
    }
    for (int j = 1; j < Ny - 1; ++j) // Y edges
    {
        p[0][(j)*Ny] =                                                                                              // AD
            De1 * (p[1][(1) + (j)*Ny] + p[1][(1) + (j)*Ny] + p[1][(j + 1)*Ny] + p[1][(j - 1)*Ny] + p[1][(j)*Ny + (1)*Ny*Nz]
                + p[1][(j)*Ny + (1)*Ny*Nz] + 2*p[1][(j)*Ny]) - De2 * p[2][(j)*Ny];
        p[0][Nx - 1 + (j)*Ny] =                                                                                             // BC
            De1 * (p[1][(Nx - 1 - 1) + (j)*Ny] + p[1][(Nx - 1 - 1) + (j)*Ny] + p[1][(Nx - 1) + (j + 1)*Ny] + p[1][(Nx - 1) + (j - 1)*Ny] + p[1][(Nx - 1) + (j)*Ny + (1)*Ny*Nz]
                + p[1][(Nx - 1) + (j)*Ny + (1)*Ny*Nz] + 2*p[1][(Nx - 1) + (j)*Ny]) - De2 * p[2][(Nx - 1) + (j)*Ny];
        p[0][(j)*Ny + (Nz - 1)*Ny*Nz] =                                                                                             // EH
            De1 * (p[1][(1) + (j)*Ny + (Nz - 1)*Ny*Nz] + p[1][(1) + (j)*Ny + (Nz - 1)*Ny*Nz] + p[1][(j + 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(j - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(j)*Ny + (Nz - 1 - 1)*Ny*Nz]
                + p[1][(j)*Ny + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][(j)*Ny + (Nz - 1)*Ny*Nz]) - De2 * p[2][(j)*Ny + (Nz - 1)*Ny*Nz];
        p[0][(Nx - 1) + (j)*Ny + (Nz - 1)*Ny*Nz] =                                                                                            // FG
            De1 * (p[1][(Nx - 1 - 1) + (j)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1 - 1) + (j)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (j + 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (j - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (j)*Ny + (Nz - 1 - 1)*Ny*Nz]
                + p[1][(Nx - 1) + (j)*Ny + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][(Nx - 1) + (j)*Ny + (Nz - 1)*Ny*Nz]) - De2 * p[2][(Nx - 1) + (j)*Ny + (Nz - 1)*Ny*Nz];
    }
    for (int k = 1; k < Nz - 1; ++k) // Z edges
    {
        p[0][(k)*Ny*Nz] =                                                                                              // AE
            De1 * (p[1][(1) + (k)*Ny*Nz] + p[1][(1) + (k)*Ny*Nz] + p[1][(1)*Ny +  (k)*Ny*Nz] + p[1][(1)*Ny + (k)*Ny*Nz] + p[1][(k + 1)*Ny*Nz]
            + p[1][(k - 1)*Ny*Nz] + 2*p[1][(k)*Ny*Nz]) - De2 * p[2][(k)*Ny*Nz];
        p[0][(Nx - 1) + (k)*Ny*Nz] =                                                                                             // BF
            De1 * (p[1][(Nx - 1 - 1) + (k)*Ny*Nz] + p[1][(Nx - 1 - 1) + (k)*Ny*Nz] + p[1][(Nx - 1) + (1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (k + 1)*Ny*Nz]
            + p[1][(Nx - 1) + (k - 1)*Ny*Nz] + 2*p[1][(Nx - 1) + (k)*Ny*Nz]) - De2 * p[2][(Nx - 1) + (k)*Ny*Nz];
        p[0][(Ny - 1)*Ny + (k)*Ny*Nz] =                                                                                             // DH
                De1 * (p[1][(1) + (Ny - 1)*Ny + (k)*Ny*Nz] + p[1][(1) + (Ny - 1)*Ny + (k)*Ny*Nz] + p[1][(Ny - 1 - 1)*Ny + (k)*Ny*Nz] + p[1][(Ny - 1 - 1)*Ny + (k)*Ny*Nz] + p[1][(Ny - 1)*Ny + (k + 1)*Ny*Nz]
                    + p[1][(Ny - 1)*Ny + (k - 1)*Ny*Nz] + 2*p[1][(Ny - 1)*Ny + (k)*Ny*Nz]) - De2 * p[2][(Ny - 1)*Ny + (k)*Ny*Nz];
        p[0][(Nx - 1) + (Ny - 1)*Ny + (k)*Ny*Nz] =                                                                                             // CG
                De1 * (p[1][(Nz - 1 - 1) + (Ny - 1)*Ny + (k)*Ny*Nz] + p[1][(Nz - 1 - 1) + (Ny - 1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (Ny - 1 - 1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (Ny - 1 - 1)*Ny + (k)*Ny*Nz] + p[1][(Nx - 1) + (Ny - 1)*Ny + (k + 1)*Ny*Nz]
                    + p[1][(Nx - 1) + (Ny - 1)*Ny + (k - 1)*Ny*Nz] + 2*p[1][(Nx - 1) + (Ny - 1)*Ny + (k)*Ny*Nz]) - De2 * p[2][(Nx - 1) + (Ny - 1)*Ny + (k)*Ny*Nz];
    }

    /* ============================================ CORNERS ===============================================================*/

    p[0][0] =                                                                                              // A
        Dc1 * (p[1][(1)] + p[1][(1)] + p[1][(1)*Ny] + p[1][(1)*Ny] + p[1][(1)*Ny*Nz]
            + p[1][(1)*Ny*Nz] + 2*p[1][0]) - Dc2 * p[2][0];
    p[0][(Nz - 1)*Ny*Nz] =                                                                                             // E
        Dc1 * (p[1][(1) + (Nz - 1)*Ny*Nz] + p[1][(1) + (Nz - 1)*Ny*Nz] + p[1][(1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nz - 1 - 1)*Ny*Nz]
            + p[1][(Nz - 1 - 1)*Ny*Nz] + 2*p[1][(Nz - 1)*Ny*Nz]) - Dc2 * p[2][(Nz - 1)*Ny*Nz];
    p[0][(Ny - 1)*Ny] =                                                                                             // D
        Dc1 * (p[1][(1) + (Ny - 1)*Ny] + p[1][(1) + (Ny - 1)*Ny] + p[1][(Ny - 1 - 1)*Ny] + p[1][(Ny - 1 - 1)*Ny] + p[1][(Ny - 1)*Ny + (1)*Ny*Nz]
            + p[1][(Ny - 1)*Ny + (1)*Ny*Nz] + 2*p[1][(Ny - 1)*Ny]) - Dc2 * p[2][(Ny - 1)*Ny];
    p[0][(Nx - 1)] =                                                                                             // B
        Dc1 * (p[1][(Nz - 1 - 1)] + p[1][(Nz - 1 - 1)] + p[1][(Nx - 1) + (1)*Ny] + p[1][(Nx - 1) + (1)*Ny] + p[1][(Nx - 1) + (1)*Ny*Nz]
            + p[1][(Nx - 1) + (1)*Ny*Nz] + 2*p[1][(Nx - 1)]) - Dc2 * p[2][(Nx - 1)];
    p[0][(Ny - 1)*Ny + (Nz - 1)*Ny*Nz] =                                                                                            // H
        Dc1 * (p[1][(1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Ny - 1 - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Ny - 1 - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Ny - 1)*Ny + (Nz - 1 - 1)*Ny*Nz]
            + p[1][(Ny - 1)*Ny + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][(Ny - 1)*Ny + (Nz - 1)*Ny*Nz]) - Dc2 * p[2][(Ny - 1)*Ny + (Nz - 1)*Ny*Nz];
    p[0][(Nx - 1) + (Nz - 1)*Ny*Nz] =                                                                                            // F
        Dc1 * (p[1][(Nz - 1 - 1) + (Nz - 1)*Ny*Nz] + p[1][(Nz - 1 - 1) + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (1)*Ny +  (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (1)*Ny +  (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (Nz - 1 - 1)*Ny*Nz]
            + p[1][(Nx - 1) + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][(Nx - 1) + (Nz - 1)*Ny*Nz]) - Dc2 * p[2][(Nx - 1) + (Nz - 1)*Ny*Nz];
    p[0][(Nx - 1) + (Ny - 1)*Ny] =                                                                                            // D
        Dc1 * (p[1][(Nz - 1 - 1) + (Ny - 1)*Ny] + p[1][(Nz - 1 - 1) + (Ny - 1)*Ny] + p[1][(Nx - 1) + (Ny - 1 - 1)*Ny] + p[1][(Nx - 1) + (Ny - 1 - 1)*Ny] + p[1][(Nx - 1) + (Ny - 1)*Ny + (1)*Ny*Nz]
            + p[1][(Nx - 1) + (Ny - 1)*Ny + (1)*Ny*Nz] + 2*p[1][(Nx - 1) + (Ny - 1)*Ny]) - Dc2 * p[2][(Nx - 1) + (Ny - 1)*Ny];
    p[0][(Nx - 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] =                                                                                           // G
        Dc1 * (p[1][(Nz - 1 - 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nz - 1 - 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (Ny - 1 - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (Ny - 1 - 1)*Ny + (Nz - 1)*Ny*Nz] + p[1][(Nx - 1) + (Ny - 1)*Ny + (Nz - 1 - 1)*Ny*Nz]
            + p[1][(Nx - 1) + (Ny - 1)*Ny + (Nz - 1 - 1)*Ny*Nz] + 2*p[1][(Nx - 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz]) - Dc2 * p[2][(Nx - 1) + (Ny - 1)*Ny + (Nz - 1)*Ny*Nz];

}
void FDS_ReverbAudioProcessor::updateStates() 
{
                double* pTmp = p[2];
                p[2] = p[1];
                p[1] = p[0];
                p[0] = pTmp;
}
//==============================================================================
bool FDS_ReverbAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* FDS_ReverbAudioProcessor::createEditor()
{
    return new FDS_ReverbAudioProcessorEditor (*this);
}

//==============================================================================
void FDS_ReverbAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void FDS_ReverbAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FDS_ReverbAudioProcessor();
}