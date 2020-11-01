/*
 * Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <chrono>
#include <atomic>
#include <signal.h>

#include <opencv2/core/core.hpp>
#include <opencv2/videoio/videoio.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <videostitcher/cameras_calib.h>

static std::string matches_window = "Matches";
static std::string warping_window = "Warping";
static int window_width = 1280;
static int window_height = 720;

static std::string create_capture (int camera, int width, int height, int fps);

static void help()
{
    std::cout << "\nThis is a sample OpenCV application to demonstrate "
    "CSI-camera capture pipeline for NVIDIA accelerated GStreamer.\n\n"
    "./opencv_nvgstcam [--Options]\n\n"
    "OPTIONS:\n"
    "\t-h,--help            Prints this message\n"
    "\t--width              Capture width [Default = 1280]\n"
    "\t--height             Capture height [Default = 720]\n"
    "\t--fps                Frames per second [Default = 30]\n"
    "\tq                    Runtime command to stop capture\n\n"
    << std::endl;

}

static std::string create_capture (int camera, int width, int height, int fps)
{
    std::stringstream pipeline_str;
    pipeline_str << "nvarguscamerasrc sensor-id=" << std::to_string(camera) 
        << " ! video/x-raw(memory:NVMM), width=(int)" << std::to_string(width) 
        << ", height=(int)" << std::to_string(height)
        << ", format=(string)NV12, framerate=(fraction)" << std::to_string(fps)
        << "/1 ! nvvidconv ! video/x-raw, format=(string)BGRx ! videoconvert"
        " ! video/x-raw, format=(string)BGR ! appsink ";

    return pipeline_str.str();
}

std::atomic<bool> g_stop;
void signal_callback_handler(int signum) 
{
   g_stop = true; 
}

int main(int argc, char const *argv[])
{
    int width;
    int height;
    unsigned int fps;
    int return_val = 0;
    double fps_calculated;
    cv::TickMeter ticks;

    cv::VideoCapture capture0, capture1;
    videostitcher::CamerasCalib::Settings calib_settings; 
    std::shared_ptr<videostitcher::CamerasCalib> calib; 

    std::vector<cv::Mat> images(2); 
    std::vector<cv::cuda::GpuMat> cuda_images(2); 
    cv::Mat matches_image; 
    cv::cuda::GpuMat stitched_image; 
    cv::Mat visual_stitching; 
    double psnr = 0; 
    cv::Scalar mssim; 

    const std::string keys =
    "{h help         |     | message }"
    "{width          |1920 | width }"
    "{height         |1080 | height }"
    "{fps            |30   | frame per second }"
    ;

    cv::CommandLineParser cmd_parser(argc, argv, keys);

    if (cmd_parser.has("help"))
    {
        help();
        goto cleanup;
    }

    width = cmd_parser.get<int>("width");
    height = cmd_parser.get<int>("height");
    fps = cmd_parser.get<unsigned int>("fps");

    if (!cmd_parser.check())
    {
        cmd_parser.printErrors();
        help();
        return_val = -1;
        goto cleanup;
    }

    capture0.open(create_capture(0, width, height, fps), cv::CAP_GSTREAMER);
    if (!capture0.isOpened())
    {
        std::cerr << "Failed to open VideoCapture for first camera" << std::endl;
        return_val = -4;
        goto cleanup;
    }

    capture1.open(create_capture(1, width, height, fps), cv::CAP_GSTREAMER);
    if (!capture1.isOpened())
    {
        std::cerr << "Failed to open VideoCapture for second camera" << std::endl;
        return_val = -4;
        goto cleanup;
    }

    calib_settings.image_size = cv::Size(width, height); 
    calib.reset(new videostitcher::CamerasCalib(calib_settings));
    if (!calib) {
        std::cerr << "Failed to start calibrator" << std::endl;
        return_val = -5;
        goto cleanup;
    }

    cv::namedWindow(matches_window, cv::WINDOW_NORMAL); 
    cv::namedWindow(warping_window, cv::WINDOW_NORMAL); 

    cv::resizeWindow(matches_window, window_width, window_height);
    cv::resizeWindow(warping_window, window_width, window_height);

    cv::moveWindow(matches_window, 200, 100); 
    cv::moveWindow(warping_window, window_width + 250, 100); 

    g_stop = false;
    signal(SIGINT, signal_callback_handler);
    while (!g_stop)
    {
        ticks.start();
        capture0 >> images[0];
        capture1 >> images[1]; 

        cuda_images[0].upload(images[0]); 
        cuda_images[1].upload(images[1]);

        calib->Feed(cuda_images); 
        calib->DrawMatches(images, matches_image); 
        calib->Evaluate(cuda_images, psnr, mssim, stitched_image); 
        stitched_image.download(visual_stitching); 
        cv::imshow(matches_window, matches_image);
        cv::imshow(warping_window, visual_stitching);
        int key = cv::waitKey(1);

        // 'q' for termination
        if (key == 'q' ) {
            break;
        }
        else if (key == 'c') {
            calib->Estimate();  
        }
        else if (key == 's') {
            calib->Save(); 
        }
        else if (key == 'r') {
            calib->Reset(); 
        }
        ticks.stop();
    }

    if (ticks.getCounter() == 0) {
        std::cerr << "No frames processed" << std::endl;
        return_val = -10;
        goto cleanup;
    }

    fps_calculated = ticks.getCounter() / ticks.getTimeSec();
    std::cout << "Fps observed " << fps_calculated << std::endl;

cleanup:
    capture0.release();
    capture1.release();
    cv::destroyAllWindows(); 
    return return_val;
}
