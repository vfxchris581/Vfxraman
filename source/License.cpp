// VFXAamps — licensing implementation.

#include "License.h"

#include <juce_cryptography/juce_cryptography.h>

namespace vfxa
{
namespace
{
// Public half of the publisher's 2048-bit RSA key ("e,n" in hex, juce::RSAKey format).
// The private half is never in this repository; it is loaded into tools/keygen/vfxa-keygen.html.
const char* const kPublicKey =
    "10001,"
    "b3bee2b75d9c1302696c79af6cfef08d423cfa0620b45998f2ecb3629ce0f64f"
    "752f4acc1ca675946eec89a80311a3e09ec16acf181eb2727776122d815472bc"
    "97c2e904884a51d063815da1b85d0cb9be566b9c001da84be2618526e8ed797d"
    "c928773014545ec61c1483a63c2198a9705730832aa238b761b1163a8b299513"
    "6202b2f5c5159d7458e44edc651efb7991c2e69d1371aff1c549dfe84122a05a"
    "8f5ad64a1adc85cb970788ac1407ec90500881aeb5a9e332e9be056668ad67e1"
    "e54d6a89c52a651f42e266594f1ee8036546ed50d28ceb1f807f2b7aa1ce06d1"
    "15b8550cb5a3be72b225dc72871ef2e8ea3d2ae853477af1cdce5cb3d4a86c15";

const char* const kProduct = "VFXAamps";
const char* const kKeyPrefix = "VFXA1.";
const char* const kSalt = "vfxa-trial-7c1e5b93";
constexpr juce::int64 kDayMs = 24 * 60 * 60 * 1000LL;

bool fromBase64 (const juce::String& text, juce::MemoryBlock& out)
{
    juce::MemoryOutputStream mo (out, false);
    return juce::Base64::convertFromBase64 (mo, text);
}

juce::String sha256Hex (const juce::String& text)
{
    const auto utf8 = text.toUTF8();
    return juce::SHA256 (utf8.getAddress(), utf8.sizeInBytes() - 1).toHexString();
}

juce::String checksum (const juce::String& value) { return sha256Hex (juce::String (kSalt) + "|" + value).substring (0, 16); }

juce::String stamp (juce::int64 ms) { return juce::String (ms) + ":" + checksum (juce::String (ms)); }

/** Reads a stamped time; returns -1 for a missing value and -2 for a tampered one. */
juce::int64 readStamp (const juce::String& s)
{
    if (s.isEmpty()) return -1;
    const auto ms = s.upToFirstOccurrenceOf (":", false, false);
    const auto sum = s.fromFirstOccurrenceOf (":", false, false);
    if (! ms.containsOnly ("0123456789") || sum != checksum (ms)) return -2;
    return ms.getLargeIntValue();
}

juce::File defaultMainFile()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("VFXAamps").getChildFile ("license.xml");
}

juce::File defaultMarkerFile()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Preferences").getChildFile (".com.viridianaudioworks.vfxa");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile (".vfxa-data");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share/.vfxa-data");
   #endif
}
} // namespace

//==============================================================================
LicenseKeyInfo verifyLicenseKey (const juce::String& keyText)
{
    LicenseKeyInfo info;
    const auto key = keyText.removeCharacters (" \t\r\n");
    if (key.isEmpty()) { info.error = "Please paste a license key."; return info; }
    if (! key.startsWith (kKeyPrefix)) { info.error = "That is not a VFXAamps license key."; return info; }

    const auto body = key.substring (juce::String (kKeyPrefix).length());
    const auto payloadB64 = body.upToFirstOccurrenceOf (".", false, false);
    const auto sigB64 = body.fromFirstOccurrenceOf (".", false, false);
    juce::MemoryBlock payloadBytes, sigBytes;
    if (payloadB64.isEmpty() || sigB64.isEmpty() || ! fromBase64 (payloadB64, payloadBytes) || ! fromBase64 (sigB64, sigBytes)
        || sigBytes.getSize() == 0 || sigBytes.getSize() > 512)
    {
        info.error = "The license key is incomplete or damaged. Copy the whole key and try again.";
        return info;
    }

    const auto payload = juce::String::fromUTF8 (static_cast<const char*> (payloadBytes.getData()), static_cast<int> (payloadBytes.getSize()));
    juce::StringArray parts;
    parts.addTokens (payload, "|", "");
    if (parts.size() != 4 || parts[0] != kProduct || parts[1] != "1" || parts[2].trim().isEmpty())
    {
        info.error = "That is not a VFXAamps license key.";
        return info;
    }

    // signature^e mod n must equal SHA-256(payload)
    const juce::String pub (kPublicKey);
    juce::BigInteger n;
    n.parseString (pub.fromFirstOccurrenceOf (",", false, false), 16);
    juce::BigInteger sig;
    sig.parseString (juce::String::toHexString (sigBytes.getData(), static_cast<int> (sigBytes.getSize()), 0), 16);
    if (sig.isZero() || sig.compareAbsolute (n) >= 0)
    {
        info.error = "This license key is not valid.";
        return info;
    }
    juce::RSAKey (pub).applyToValue (sig);
    juce::BigInteger expected;
    expected.parseString (sha256Hex (payload), 16);
    if (sig != expected)
    {
        info.error = "This license key is not valid.";
        return info;
    }

    info.valid = true;
    info.licensee = parts[2].trim();
    info.issued = parts[3];
    return info;
}

//==============================================================================
LicenseManager::LicenseManager() : mainFile (defaultMainFile()), markerFile (defaultMarkerFile())
{
    refresh();
}

void LicenseManager::setStorageForTesting (const juce::File& m, const juce::File& k)
{
    mainFile = m;
    markerFile = k;
    refresh();
}

void LicenseManager::setClockForTesting (std::function<juce::Time()> clock)
{
    now = std::move (clock); // takes effect on the next refresh (set the clock before the storage)
}

juce::int64 LicenseManager::readTrialStart() const
{
    // Earliest untampered start of the two copies. Both tampered (or one tampered, one missing) -> treated as long expired.
    juce::int64 best = -1;
    bool tampered = false;
    for (const auto& s : { [&] {
                              auto xml = juce::XmlDocument::parse (mainFile);
                              return xml != nullptr ? xml->getStringAttribute ("start") : juce::String();
                          }(),
                           markerFile.existsAsFile() ? markerFile.loadFileAsString().trim() : juce::String() })
    {
        const auto v = readStamp (s);
        if (v == -2) tampered = true;
        else if (v >= 0) best = best < 0 ? v : std::min (best, v);
    }
    if (best < 0) return tampered ? 1 : 0; // 1 ms after the epoch = expired
    return best;
}

void LicenseManager::writeTrialData (juce::int64 startMs, juce::int64 lastSeenMs) const
{
    auto xml = juce::XmlDocument::parse (mainFile);
    if (xml == nullptr || ! xml->hasTagName ("VFXALicense")) xml = std::make_unique<juce::XmlElement> ("VFXALicense");
    xml->setAttribute ("start", stamp (startMs));
    xml->setAttribute ("seen", stamp (lastSeenMs));
    mainFile.getParentDirectory().createDirectory();
    xml->writeTo (mainFile);
    markerFile.getParentDirectory().createDirectory();
    markerFile.replaceWithText (stamp (startMs));
}

void LicenseManager::refresh()
{
    auto xml = juce::XmlDocument::parse (mainFile);
    const auto key = xml != nullptr ? xml->getStringAttribute ("key") : juce::String();
    if (key.isNotEmpty())
    {
        const auto info = verifyLicenseKey (key);
        if (info.valid)
        {
            state = State::licensed;
            licensee = info.licensee;
            daysLeft = 0;
            audioAllowed.store (true);
            return;
        }
    }
    licensee = {};

    const auto t = now().toMilliseconds();
    auto start = readTrialStart();
    auto seen = xml != nullptr ? readStamp (xml->getStringAttribute ("seen")) : -1;
    if (start == 0) // first run: the trial starts now
    {
        start = t;
        seen = t;
        writeTrialData (start, seen);
    }
    else if (start > 1 && seen < 0)
    {
        seen = start; // main file lost its "seen" stamp; rebuild it from the start
    }

    const bool clockRolledBack = t < start - kDayMs || (seen > 0 && t < seen - kDayMs);
    const auto elapsedDays = static_cast<int> (std::max<juce::int64> (0, t - start) / kDayMs);
    daysLeft = clockRolledBack || start == 1 ? 0 : std::max (0, kTrialDays - elapsedDays);
    state = daysLeft > 0 ? State::trial : State::expired;
    audioAllowed.store (state != State::expired);

    if (start > 1 && seen >= 0 && t > seen + kDayMs / 24) writeTrialData (start, t); // remember the latest time seen (hourly)
}

juce::String LicenseManager::activate (const juce::String& key)
{
    const auto info = verifyLicenseKey (key);
    if (! info.valid) return info.error;
    auto xml = juce::XmlDocument::parse (mainFile);
    if (xml == nullptr || ! xml->hasTagName ("VFXALicense")) xml = std::make_unique<juce::XmlElement> ("VFXALicense");
    xml->setAttribute ("key", key.removeCharacters (" \t\r\n"));
    mainFile.getParentDirectory().createDirectory();
    if (! xml->writeTo (mainFile)) return "Couldn't save the license to " + mainFile.getFullPathName();
    refresh();
    return {};
}

void LicenseManager::deactivate()
{
    if (auto xml = juce::XmlDocument::parse (mainFile))
    {
        xml->removeAttribute ("key");
        xml->writeTo (mainFile);
    }
    refresh();
}

} // namespace vfxa
