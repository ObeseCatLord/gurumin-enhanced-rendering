#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
namespace gurumin {
// Render-only observer. A rejected cursor observation must never disable
// compensation against the native phase that the HUD actually used.
struct RhythmClock {
  enum Reason { Accepted, Outlier, Missing, NewSong, Gap, Paused, Reacquired };
  double phase = 0, time = 0, innovation = 0;
  int song = -1;
  Reason reason = Missing;
  bool ready = false, reacquiring = false, wasPlaying = true;
  double candidateOffset = 0, candidateObservation = 0, candidateSince = 0;
  unsigned distinct = 0;
  void clearCandidate() { distinct = 0; }
  bool update(int id, double now, double omega, double observation,
              bool playing = true) {
    bool finite = std::isfinite(observation);
    if (!ready || song != id) {
      if (!finite) {
        reason = Missing;
        return false;
      }
      phase = observation;
      time = now;
      song = id;
      ready = true;
      reacquiring = false;
      wasPlaying = playing;
      clearCandidate();
      reason = NewSong;
      return true;
    }
    double dt = now - time;
    time = now; // Also advance timestamps for missing/rejected observations.
    if (!playing) {
      wasPlaying = false;
      clearCandidate();
      reason = Paused;
      return true;
    }
    if (!wasPlaying) {
      reacquiring = true;
      clearCandidate();
      wasPlaying = true;
    }
    if (dt < 0 || dt > .2) {
      reacquiring = true;
      clearCandidate();
      dt = 0;
      reason = Gap;
    }
    phase += omega * dt;
    if (!finite) {
      reason = Missing;
      return true;
    }
    innovation = observation - phase;
    if (!reacquiring && std::abs(innovation) <= 4.) {
      phase +=
          std::clamp(innovation * dt / .15, -.1 * omega * dt, .1 * omega * dt);
      clearCandidate();
      reason = Accepted;
      return true;
    }
    // A new trajectory requires distinct advancing cursor observations for
    // longer than the demonstrated ~34ms bad-cursor interval. Repeated values
    // from adjacent Presents cannot establish an epoch.
    if (!distinct || std::abs(innovation - candidateOffset) > .5 ||
        observation < candidateObservation) {
      candidateOffset = innovation;
      candidateObservation = observation;
      candidateSince = now;
      distinct = 1;
    } else if (observation > candidateObservation + 1e-5) {
      candidateObservation = observation;
      ++distinct;
    }
    reason = Outlier;
    if (distinct >= 3 && now - candidateSince >= .15) {
      phase = observation;
      reacquiring = false;
      clearCandidate();
      reason = Reacquired;
    }
    return true;
  }
};
inline double rhythmBounce(double phase) {
  constexpr double curve[] = {0,  11, 20, 27, 32, 35, 37, 38,
                              37, 35, 32, 27, 20, 11, 0};
  double wrapped = std::fmod(phase, 15.);
  if (wrapped < 0)
    wrapped += 15.;
  int i = int(wrapped);
  return 13. -
         (curve[i] + (wrapped - i) * (curve[(i + 1) % 15] - curve[i])) / 2.;
}
inline double nativeRhythmBounce(int phase) {
  constexpr int curve[] = {0,  11, 20, 27, 32, 35, 37, 38,
                           37, 35, 32, 27, 20, 11, 0};
  return 13 - curve[(phase % 15 + 15) % 15] / 2;
}
} // namespace gurumin
