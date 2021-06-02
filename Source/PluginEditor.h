/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class FDS_ReverbAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    FDS_ReverbAudioProcessorEditor (FDS_ReverbAudioProcessor&);
    ~FDS_ReverbAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    FDS_ReverbAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FDS_ReverbAudioProcessorEditor)
};
