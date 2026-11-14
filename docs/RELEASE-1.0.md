# FM1 Kids 1.0 release candidate

This release keeps the existing controls and adds no new play modes.

- Release held notes when entering the parent Bluetooth panel.
- Restore the previous octave, scale and chord setting when leaving Play Along.
- Leave Play Along when opening the song picker.
- Guard the end of a completed tune and short MIDI messages before reading them.
- Correct the installation version, undo instructions, autosave instructions and song-part guide.
- Link the installer to controls and source matching its exact build.

## Release verification

The 1.0 candidate must pass the Build workflow: version check, firmware and
browser preview, flash-size checks, emulator suites and Docker build.
The parent-panel regression exercises a held organ note and checks it becomes quiet.

Before describing 1.0 as hardware tested, check on an FM-1: boot and play every
instrument, release a held note while holding GLO, enter and leave Play Along
from a changed octave, record and undo a pattern, save while stopped and power
cycle, and connect a MIDI keyboard. Record the tested commit and device here.
No physical-device test of this candidate has been performed in this review.

After checks pass, merge to main to publish the installer through GitHub Pages.
Verify the Pages deployment itself succeeded and the installer displays 1.0;
a green build alone is insufficient because the Pages job currently allows failure.
