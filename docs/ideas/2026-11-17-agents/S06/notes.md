# About lines

The offer lives in the bottom-right status pill after the user has opened three papers. One click reads the folder in the background and adds one dim line beneath every filename, including Persian files. Search matches and highlights those lines; grid view carries them below the names.
Reopening Papers preserves its descriptions. A simulated new scan is the only file read on the second run. View Options lets the viewer turn lines off or clear them, with Undo for clearing. The full runner passed in light and dark; both sheets were opened and inspected. Behavior checks and grid captures also passed at 1280 × 800 in both themes, including Persian search, selection while reading, caching, suppression after dismissal, and clear/Undo.

## Not made

- This is a browser mock. Claude Code reading and the arrival of a new scan are simulated; no real papers, agents or filesystem are accessed.
- Browser localStorage stands in for one small local description file per folder. File revisions model the new-or-changed check; real modification timestamps are not read.
- The sample starts just after the three file openings, so its first frame already contains the offer. Double-clicking files still opens a mock preview.
- The offer expires after 45 seconds rather than a few seconds, to leave time to read the guided sample. Dismissal is remembered for the day. Restart resets the demo.

- Native integration, production performance, a phone layout, and the visual skill's separate measured audit were not tested. The supplied desktop runner was used for this fixed-size sample. No known broken control remains in the guided path.
- Temporary files and logs are left for the lead to tidy, as requested. Nothing was deleted.
