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
    "be21b77efddf519ca92c662a1cb3c04617e9f7a5df81f095723d7a261e10d631"
    "03e83773ad1556ba628db03acdd11c7192f00dbfd902b76073af5c9388195a45"
    "7d7ff169bd0f092492d876169ce0866934fa49e0121d94c94dbdbf03db780986"
    "0cc0508604f6a6fbe7bfe4262a8c852127e9289a2fda99367a3061975e3c9ad3"
    "201170ca5720400d6fdede21db16da7d3a374b48b5717fbe0a8c2875183037c0"
    "30448385b5a831b1b0b06d45fb17080b7341e2525acf905fb5240f748b696224"
    "8ca7e4f911fb7d15f96ddf10628e7803d1c69965cc697a0ecc79c0a7941e50b7"
    "f8a40ea36e87be74f6c498212e86941ad44892e89469d729950addf7c2a20529";

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
