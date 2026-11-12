// VFXAamps — licensing: signed offline keys and a 14-day trial.
//
// A key is "VFXA1.<payload>.<signature>" (both standard Base64). The payload is the UTF-8 text
// "VFXAamps|1|<licensee>|<YYYY-MM-DD>"; the signature is SHA-256(payload)^d mod n with the
// publisher's private RSA key. The plug-in only holds the public key, so it can check keys but
// cannot make them. Keys are made with tools/keygen/vfxa-keygen.html.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

namespace vfxa
{

struct LicenseKeyInfo
{
    bool valid = false;
    juce::String licensee, issued, error;
};

/** Pure key check (any thread). */
LicenseKeyInfo verifyLicenseKey (const juce::String& key);

/** License + trial state, shared by every plug-in instance in the process (juce::SharedResourcePointer).
    Message thread only, except isAudioAllowed(). */
class LicenseManager
{
public:
    static constexpr int kTrialDays = 14;
    enum class State { licensed, trial, expired };

    LicenseManager();

    /** Re-reads the stored key / trial dates (cheap; also picks up an activation done in another app). */
    void refresh();

    /** Checks and stores a key. Returns an empty string on success, otherwise the reason it was rejected. */
    juce::String activate (const juce::String& key);
    void deactivate();

    State getState() const noexcept { return state; }
    int getTrialDaysLeft() const noexcept { return daysLeft; }
    juce::String getLicensee() const { return licensee; }

    /** Audio thread: false once the trial has run out and no valid key is stored. */
    bool isAudioAllowed() const noexcept { return audioAllowed.load (std::memory_order_relaxed); }

    // Tests: a fake clock (set first; used from the next refresh) and other storage files (refreshes).
    void setStorageForTesting (const juce::File& mainFile, const juce::File& markerFile);
    void setClockForTesting (std::function<juce::Time()> clock);

private:
    juce::File mainFile, markerFile;
    std::function<juce::Time()> now { [] { return juce::Time::getCurrentTime(); } };
    State state = State::trial;
    int daysLeft = kTrialDays;
    juce::String licensee;
    std::atomic<bool> audioAllowed { true };

    juce::int64 readTrialStart() const;          // ms since epoch, 0 if none
    void writeTrialData (juce::int64 startMs, juce::int64 lastSeenMs) const;
};

} // namespace vfxa
