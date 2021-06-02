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
        pStates.push_back  (std::vector<std::vector<std::vector<double>>> (Nx+1,
                            std::vector<std::vector<double>> (Ny+1,
                            std::vector<double> (Nz+1, 0.0))));



    p.reserve(3);

    for (int i = 0; i < 3; ++i)
        p.push_back(std::vector<std::vector<double*>>(Nx + 1,
                    std::vector<double*>(Ny + 1, nullptr)));

    

    for (int n = 0; n < 3; ++n)
        for (int i = 0; i < Nx + 1; ++i)
            for (int j = 0; j < Ny + 1; ++j)
                p[n][i][j] = &pStates[n][i][j][0];




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
            p[1][3][3][3] += vin;
            calculateScheme();
            updateStates();
            vout = p[1][3][3][3];
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

    for (int i = 1; i < Nx; ++i)            // not including the boundaries
    {
        for (int j = 1; j < Ny; ++j)        // not including the boundaries
        {
            for (int k = 1; k < Nz; ++k)    // not including the boundaries
            {
                p[0][i][j][k] =
                    Dg1 * (p[1][i + 1][j][k] + p[1][i - 1][j][k] + p[1][i][j + 1][k] + p[1][i][j - 1][k] + p[1][i][j][k + 1]
                           + p[1][i][j][k - 1] + 2 * p[1][i][j][k]) - Dg2 * p[2][i][j][k];
            }
        }
    }
    /*=================================================== INTERIOR =========================================================================*/
    for (int i = 1; i < Nx; ++i) // Top and bottom faces
    {
        for (int j = 1; j < Ny; ++j)
        {
            p[0][i][j][0] =                                                                                              // Bottom ABCD
                Di1 * (p[1][i + 1][j][0] + p[1][i - 1][j][0] + p[1][i][j + 1][0] + p[1][i][j - 1][0] + p[1][i][j][0 + 1]
                    + p[1][i][j][0 + 1] + 2 * p[1][i][j][0]) - Di2 * p[2][i][j][0]; 
            p[0][i][j][Nz] =                                                                                             // Top EFGH
                Di1 * (p[1][i + 1][j][Nz] + p[1][i - 1][j][Nz] + p[1][i][j + 1][Nz] + p[1][i][j - 1][Nz] + p[1][i][j][Nz - 1]
                    + p[1][i][j][Nz - 1] + 2 * p[1][i][j][Nz]) - Di2 * p[2][i][j][Nz];
        }
    }
    for (int i = 1; i < Nx; ++i) // Front and Back faces
    {
        for (int k = 1; k < Nz; ++k)
        {
            p[0][i][0][k] =                                                                                              // Front AFEF
                Di1 * (p[1][i + 1][0][k] + p[1][i - 1][0][k] + p[1][i][0 + 1][k] + p[1][i][0 + 1][k] + p[1][i][0][k + 1]
                    + p[1][i][0][k - 1] + 2 * p[1][i][0][k]) - Di2 * p[2][i][0][k];
            p[0][i][Ny][k] =                                                                                             // Back CDGH
                Di1 * (p[1][i + 1][Ny][k] + p[1][i - 1][Ny][k] + p[1][i][Ny - 1][k] + p[1][i][Ny - 1][k] + p[1][i][Ny][k + 1]
                    + p[1][i][Ny][k - 1] + 2 * p[1][i][Ny][k]) - Di2 * p[2][i][Ny][k];
        }
    }
    for (int j = 1; j < Nx; ++j) // Left and Right faces
    {
        for (int k = 1; k < Nz; ++k)
        {
            p[0][0][j][k] =                                                                                              // Left ADEH
                Di1 * (p[1][0 + 1][j][k] + p[1][0 + 1][j][k] + p[1][0][j + 1][k] + p[1][0][j - 1][k] + p[1][0][j][k + 1]
                    + p[1][0][j][k - 1] + 2 * p[1][0][j][k]) - Di2 * p[2][0][j][k];
            p[0][Nx][j][k] =                                                                                             // Right BCFG
                Di1 * (p[1][Nx - 1][j][k] + p[1][Nx - 1][j][k] + p[1][Nx][j + 1][k] + p[1][Nx][j - 1][k] + p[1][Nx][j][k + 1]
                    + p[1][Nx][j][k - 1] + 2 * p[1][Nx][j][k]) - Di2 * p[2][Nx][j][k];
        }
    }
    /*=================================================== EDGES =========================================================================*/
  
    for (int i = 1; i < Nx; ++i) // X edges
    {
        p[0][i][0][0] =                                                                                              // AB
            De1 * (p[1][i + 1][0][0] + p[1][i - 1][0][0] + p[1][i][0 + 1][0] + p[1][i][0 + 1][0] + p[1][i][0][0 + 1]
                + p[1][i][0][0 + 1] + 2 * p[1][i][0][0]) - De2 * p[2][i][0][0];
        p[0][i][Ny][0] =                                                                                             // CD
            De1 * (p[1][i + 1][Ny][0] + p[1][i - 1][Ny][0] + p[1][i][Ny - 1][0] + p[1][i][Ny - 1][0] + p[1][i][Ny][0 + 1]
                + p[1][i][Ny][0 + 1] + 2 * p[1][i][Ny][0]) - De2 * p[2][i][Ny][0];
        p[0][i][0][Nz] =                                                                                             // EF
            De1 * (p[1][i + 1][0][Nz] + p[1][i - 1][0][Nz] + p[1][i][0 + 1][Nz] + p[1][i][0 + 1][Nz] + p[1][i][0][Nz - 1]
                + p[1][i][0][Nz - 1] + 2 * p[1][i][0][Nz]) - De2 * p[2][i][0][Nz];
        p[0][i][Ny][Nz] =                                                                                            // GH
            De1 * (p[1][i + 1][Ny][Nz] + p[1][i - 1][Ny][Nz] + p[1][i][Ny - 1][Nz] + p[1][i][Ny - 1][Nz] + p[1][i][Ny][Nz - 1]
                + p[1][i][Ny][Nz - 1] + 2 * p[1][i][Ny][Nz]) - De2 * p[2][i][Ny][Nz];
    }
    for (int j = 1; j < Ny; ++j) // Y edges
    {
        p[0][0][j][0] =                                                                                              // AD
            De1 * (p[1][0 + 1][j][0] + p[1][0 + 1][j][0] + p[1][0][j + 1][0] + p[1][0][j - 1][0] + p[1][0][j][0 + 1]
                + p[1][0][j][0 + 1] + 2 * p[1][0][j][0]) - De2 * p[2][0][j][0];
        p[0][Nx][j][0] =                                                                                             // BC
            De1 * (p[1][Nx - 1][j][0] + p[1][Nx - 1][j][0] + p[1][Nx][j + 1][0] + p[1][Nx][j - 1][0] + p[1][Nx][j][0 + 1]
                + p[1][Nx][j][0 + 1] + 2 * p[1][Nx][j][0]) - De2 * p[2][Nx][j][0];
        p[0][0][j][Nz] =                                                                                             // EH
            De1 * (p[1][0 + 1][j][Nz] + p[1][0 + 1][j][Nz] + p[1][0][j + 1][Nz] + p[1][0][j - 1][Nz] + p[1][0][j][Nz - 1]
                + p[1][0][j][Nz - 1] + 2 * p[1][0][j][Nz]) - De2 * p[2][0][j][Nz];
        p[0][Nx][j][Nz] =                                                                                            // FG
            De1 * (p[1][Nx - 1][j][Nz] + p[1][Nx - 1][j][Nz] + p[1][Nx][j + 1][Nz] + p[1][Nx][j - 1][Nz] + p[1][Nx][j][Nz - 1]
                + p[1][Nx][j][Nz - 1] + 2 * p[1][Nx][j][Nz]) - De2 * p[2][Nx][j][Nz];
    }
    for (int k = 1; k < Nz; ++k) // Z edges
    {
        p[0][0][0][k] =                                                                                              // AE
            De1 * (p[1][0 + 1][0][k] + p[1][0 + 1][0][k] + p[1][0][0 + 1][k] + p[1][0][0 + 1][k] + p[1][0][0][k + 1]
                + p[1][0][0][k - 1] + 2 * p[1][0][0][k]) - De2 * p[2][0][0][k];
        p[0][Nx][0][k] =                                                                                             // BF
            De1 * (p[1][Nx - 1][0][k] + p[1][Nx - 1][0][k] + p[1][Nx][0 + 1][k] + p[1][Nx][0 + 1][k] + p[1][Nx][0][k + 1]
                + p[1][Nx][0][k - 1] + 2 * p[1][Nx][0][k]) - De2 * p[2][Nx][0][k];
        p[0][0][Ny][k] =                                                                                             // DH
            De1 * (p[1][0 + 1][Ny][k] + p[1][0 + 1][Ny][k] + p[1][0][Ny - 1][k] + p[1][0][Ny - 1][k] + p[1][0][Ny][k + 1]
                + p[1][0][Ny][k - 1] + 2 * p[1][0][Ny][k]) - De2 * p[2][0][Ny][k];
        p[0][0][Ny][k] =                                                                                             // CG
            De1 * (p[1][Nx - 1][Ny][k] + p[1][Nx - 1][Ny][k] + p[1][Nx][Ny - 1][k] + p[1][Nx][Ny - 1][k] + p[1][Nx][Ny][k + 1]
                + p[1][Nx][Ny][k - 1] + 2 * p[1][Nx][Ny][k]) - De2 * p[2][Nx][Ny][k];
    }

    /* ============================================ CORNERS ===============================================================*/ 
    
    p[0][0][0][0] =                                                                                              // A
        Dc1 * (p[1][0 + 1][0][0] + p[1][0 + 1][0][0] + p[1][0][0 + 1][0] + p[1][0][0 + 1][0] + p[1][0][0][0 + 1]
            + p[1][0][0][0 + 1] + 2 * p[1][0][0][0]) - Dc2 * p[2][0][0][0];
    p[0][0][0][Nz] =                                                                                             // E
        Dc1 * (p[1][0 + 1][0][Nz] + p[1][0 + 1][0][Nz] + p[1][0][0 + 1][Nz] + p[1][0][0 + 1][Nz] + p[1][0][0][Nz - 1]
            + p[1][0][0][Nz - 1] + 2 * p[1][0][0][Nz]) - Dc2 * p[2][0][0][Nz];
    p[0][0][Ny][0] =                                                                                             // D
        Dc1 * (p[1][0 + 1][Ny][0] + p[1][0 + 1][Ny][0] + p[1][0][Ny - 1][0] + p[1][0][Ny - 1][0] + p[1][0][Ny][0 + 1]
            + p[1][0][Ny][0 + 1] + 2 * p[1][0][Ny][0]) - Dc2 * p[2][0][Ny][0];
    p[0][Nx][0][0] =                                                                                             // B
        Dc1 * (p[1][Nx - 1][0][0] + p[1][Nx - 1][0][0] + p[1][Nx][0 + 1][0] + p[1][Nx][0 + 1][0] + p[1][Nx][0][0 + 1]
            + p[1][Nx][0][0 + 1] + 2 * p[1][Nx][0][0]) - Dc2 * p[2][Nx][0][0];
    p[0][0][Ny][Nz] =                                                                                            // H
        Dc1 * (p[1][0 + 1][Ny][Nz] + p[1][0 + 1][Ny][Nz] + p[1][0][Ny - 1][Nz] + p[1][0][Ny - 1][Nz] + p[1][0][Ny][Nz - 1]
            + p[1][0][Ny][Nz - 1] + 2 * p[1][0][Ny][Nz]) - Dc2 * p[2][0][Ny][Nz];
    p[0][Nx][0][Nz] =                                                                                            // F
        Dc1 * (p[1][Nx - 1][0][Nz] + p[1][Nx - 1][0][Nz] + p[1][Nx][0 + 1][Nz] + p[1][Nx][0 + 1][Nz] + p[1][Nx][0][Nz - 1]
            + p[1][Nx][0][Nz - 1] + 2 * p[1][Nx][0][Nz]) - Dc2 * p[2][Nx][0][Nz];
    p[0][Nx][Ny][0] =                                                                                            // D
        Dc1 * (p[1][Nx - 1][Ny][0] + p[1][Nx - 1][Ny][0] + p[1][Nx][Ny - 1][0] + p[1][Nx][Ny - 1][0] + p[1][Nx][Ny][0 + 1]
            + p[1][Nx][Ny][0 + 1] + 2 * p[1][Nx][Ny][0]) - Dc2 * p[2][Nx][Ny][0];
    p[0][Nx][Ny][Nz] =                                                                                           // G
        Dc1 * (p[1][Nx - 1][Ny][Nz] + p[1][Nx - 1][Ny][Nz] + p[1][Nx][Ny - 1][Nz] + p[1][Nx][Ny - 1][Nz] + p[1][Nx][Ny][Nz - 1]
            + p[1][Nx][Ny][Nz - 1] + 2 * p[1][Nx][Ny][Nz]) - Dc2 * p[2][Nx][Ny][Nz];

}
void FDS_ReverbAudioProcessor::updateStates() 
{
    for (int i = 0; i <= Nx; ++i)
    {
        for (int j = 0; j <= Ny; ++j)
        {
                double* pTmp = p[2][i][j];
                p[2][i][j] = p[1][i][j];
                p[1][i][j] = p[0][i][j];
                p[0][i][j] = pTmp;
        }
    }
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
