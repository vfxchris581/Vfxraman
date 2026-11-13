Nam KNoB — NAM amp head with a 10-slot morphing gain knob and a dual IR loader

INSTALL
  1. Standalone app: drag Nam KNoB.app onto the Applications folder.
  2. Plug-ins: double-click "Install Nam KNoB Plug-ins.pkg".
     VST3  -> /Library/Audio/Plug-Ins/VST3/Nam KNoB.vst3
     AU    -> /Library/Audio/Plug-Ins/Components/Nam KNoB.component
  3. Rescan plug-ins in your DAW.

Note: Nam KNoB used to be called VFXAamps. The plug-in installer removes the old VFXAamps VST3 / AU;
delete the old VFXAamps.app from Applications yourself. Your presets move from Documents/VFXAamps to
Documents/Nam KNoB the first time you open Nam KNoB.

IF macOS BLOCKS IT
  Builds without an Apple Developer ID signature are "ad-hoc" signed. The first time:
  - App / installer: right-click it, choose Open, then Open again.
    (Or System Settings > Privacy & Security > "Open Anyway".)
  - If a DAW still refuses the plug-in, run in Terminal:
      sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Nam KNoB.vst3"
      sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Nam KNoB.component"

USING IT
  - Drop .nam files on the ten slots (several at once fill the following slots).
  - Turn GAIN 1-10 to morph between neighbouring slots (1.5 = slots 1 and 2 equally).
    Empty slots are skipped.
  - Drop cab IRs (.wav, any sample rate) on IR A / IR B; BLEND mixes them.
  - Presets are saved in Documents/Nam KNoB/Presets and remember the model and IR files.
