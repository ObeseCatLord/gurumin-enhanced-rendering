#include "../src/rhythm.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using gurumin::RhythmClock;
int main() {
  RhythmClock c;
  constexpr double rate = 17., step = 1. / 175.;
  assert(c.update(1, 0, rate, 1109.));
  double previousY = gurumin::rhythmBounce(c.phase);
  for (int i = 1; i <= 80; ++i) {
    double t = i * step, observed = 1109. + rate * t;
    if (i >= 10 && i <= 15)
      observed += 10.; // Demonstrated short forward cursor error.
    if (i == 22)
      observed = std::numeric_limits<double>::quiet_NaN();
    double before = c.phase;
    assert(c.update(1, t, rate, observed));
    assert(c.time == t && c.phase > before);
    assert(c.phase - before <= 1.101 * rate * step);
    int native = int(1109. + rate * t) + (i >= 10 && i <= 15 ? 10 : 0);
    double finalY = gurumin::nativeRhythmBounce(native) +
                    gurumin::rhythmBounce(c.phase) -
                    gurumin::nativeRhythmBounce(native);
    assert(std::abs(finalY - gurumin::rhythmBounce(c.phase)) < 1e-9);
    assert(std::abs(finalY - previousY) < .7);
    previousY = finalY;
  }
  // Repeating one bad sample for many Presents must not confirm a seek.
  double base = c.phase;
  double now = c.time;
  for (int i = 1; i <= 40; ++i)
    c.update(1, now + i * step, rate, base + 100.);
  assert(c.phase < base + 10.);
  // A confirmed new advancing trajectory may establish a loop/seek epoch.
  now = c.time;
  double newPhase = 100.;
  for (int i = 1; i <= 40; ++i)
    c.update(1, now + i * step, rate, newPhase + rate * i * step);
  assert(c.phase < 110. && c.phase > 100.);
  // Pause freezes prediction; resume/gaps request consistent reacquisition.
  base = c.phase;
  now = c.time;
  c.update(1, now + .1, rate, base, false);
  assert(c.phase == base);
  c.update(1, now + .2, rate, base, false);
  assert(c.phase == base);
  c.update(1, now + .21, rate, base + .17, true);
  assert(c.reacquiring);
  c.update(1, now + 2, rate, 999.);
  assert(c.reacquiring && c.phase < base + 10.);
  // Song changes establish an explicit playback epoch immediately.
  c.update(2, now + 2.01, rate, 5.);
  assert(c.phase == 5. && c.song == 2);
  // Accepted negative innovation cannot reverse the observer.
  base = c.phase;
  now = c.time;
  c.update(2, now + step, rate, base - 3.);
  assert(c.phase > base && c.phase - base >= .899 * rate * step);
}
