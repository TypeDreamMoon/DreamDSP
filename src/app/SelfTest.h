#pragma once

class QObject;

namespace dreamdsp {

// `DreamDSP.exe --selftest` exercises the pieces that have no GUI and prints a
// report to stdout. Returns the number of failures (0 == all good), so it can
// be used as an exit code.
//
// This exists because the interesting logic -- parsing every .peace file on the
// machine, round-tripping them, and emitting APO text -- is exactly the part
// that cannot be checked by looking at a screenshot.
int runSelfTest();

// `DreamDSP.exe --slidertest` drives the first equalizer slider with synthetic
// mouse events and reports whether the model value actually moved.
//
// Dragging is the one interaction that cannot be verified from a screenshot,
// and HusSlider's value plumbing is subtle enough to get wrong twice -- so it
// gets a test that posts real QMouseEvents into the live scene.
// Returns the number of failures.
int runSliderTest(QObject *rootObject);

} // namespace dreamdsp
