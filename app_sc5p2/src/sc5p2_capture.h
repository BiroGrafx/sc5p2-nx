// Space Channel 5 Part 2: video recordings of the game image for test runs, taken from the presenter's guest output
// (what the game presents), so nothing covering or minimizing the window affects them. Same raw format as sa2-nx's
// recorder (app/src/sa2_capture.cpp): tools/make_video.sh turns captures/<name>.rgba into an MP4 and a contact sheet.
//
//   sc5p2_video_s = N               record sc5p2_video_frames frames starting N seconds after start
//   sc5p2_video_trigger_file = <f>  live: when <f> appears, record the number of frames written in it (default
//                                   sc5p2_video_frames), then delete it; the recording is captures/live_<n>.rgba

#pragma once

#include <functional>

namespace rex::ui {
class Presenter;
}

namespace sc5p2::capture {

// Starts the recorder thread when sc5p2_video_s > 0 or a trigger file is configured.
void Start(std::function<rex::ui::Presenter*()> get_presenter);

// Stops it (what was recorded so far is kept).
void Stop();

}  // namespace sc5p2::capture
