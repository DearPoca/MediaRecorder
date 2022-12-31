#include <chrono>
#include <deque>
#include <fstream>
#include <thread>

#include "logger.h"
#include "media_decoder_common.h"
#include "media_decoder_interface.h"
#include "media_recorder_common.h"
#include "media_recorder_interface.h"
#include "ring_fifo.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("usage %s input_file output_dir\n", argv[0]);
        exit(-1);
    }
    MediaDecoder* dec = MediaDecoder::CreateVideoDecoder();
    VideoDecoderStartParam dec_param;
    dec_param.filename = argv[1];
    dec->Start(&dec_param);

    int buffer_size = 120;
    std::deque<MediaDecoder::Frame*> que_frame_empty;
    std::deque<MediaDecoder::Frame*> que_frame_full;
    for (int i = 0; i < buffer_size; ++i) {
        MediaDecoder::Frame* frame = nullptr;
        dec->InitFrame(&frame);
        que_frame_empty.push_back(frame);
    }

    auto get_darkness = [&](MediaDecoder::Frame* f) {
        double sum = 255.0 * f->len;
        for (int i = 0; i < f->len; ++i) {
            sum -= f->data[i];
        }
        return sum / (255.0 * f->len);
    };

    int frame_cnt = 0;
    int file_cnt = 0;
    const double darkness_threshold = 0.99;
    const int frame_threshold = 300;

    MediaRecorder* recorder = MediaRecorder::CreateMP4VideoRecorder();
    MP4VideoRecorderStartParam rec_param;
    rec_param.width = dec_param.width;
    rec_param.height = dec_param.height;
    rec_param.fps = dec_param.fps;
    rec_param.filename = std::string(argv[2]) + "/" + "output_" + std::to_string(file_cnt) + ".mp4";
    recorder->Start(&rec_param);

    while (true) {
        MediaDecoder::Frame* frame;
        if (que_frame_empty.empty()) {
            frame = que_frame_full.front();
            que_frame_full.pop_front();
            recorder->SendVideoFrameBlock(frame->data, frame->len);
            que_frame_empty.push_back(frame);
        }
        frame = que_frame_empty.front();
        que_frame_empty.pop_front();
        if (!dec->ReadFrame(frame)) {
            break;
        }
        if (frame->len > 0) {
            que_frame_full.push_back(frame);
            ++frame_cnt;
            if (frame_cnt > frame_threshold && get_darkness(frame) > darkness_threshold) {
                recorder->Stop();
                frame_cnt = 0;
                file_cnt++;
                rec_param.width = dec_param.width;
                rec_param.height = dec_param.height;
                rec_param.fps = dec_param.fps;
                rec_param.filename = std::string(argv[2]) + "/" + "output_" + std::to_string(file_cnt) + ".mp4";
                log_info("timestamp: %ld, len: %d, darkness: %lf, new file: %s", frame->timestamp, frame->len,
                         get_darkness(frame), rec_param.filename.c_str());
                recorder->Start(&rec_param);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    while (!que_frame_full.empty()) {
        MediaDecoder::Frame* frame = que_frame_full.front();
        que_frame_full.pop_front();
        recorder->SendVideoFrameBlock(frame->data, frame->len);
    }
    recorder->Stop();
    return 0;
}