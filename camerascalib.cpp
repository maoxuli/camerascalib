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
    std::cout << "\nThis is a calibration tool running on Jetson Nano "
    "or Jetson Xvier NX to generate transform between two CSI-cameras.\n\n" 
    "./camerascalib [--Options]\n\n"
    "OPTIONS:\n"
    "\t-h,--help            Prints this message\n"
    "\t--width              Capture width [Default = 1920]\n"
    "\t--height             Capture height [Default = 1080]\n"
    "\t--fps                Frames per second [Default = 30]\n"
    "\t--out                Output calibration (path and) filename [Default = cameras.xml]\n"
    "\tc                    Runtime command to do a calibration\n"
    "\ts                    Runtime command to save current transform\n"
    "\tr                    Runtime command to reset (restart) calibration\n"
    "\tq                    Runtime command to stop capture and quit\n\n"
    "Example:\n"
    "./camerascalib --width=1920 --height=1080 --fps=30 --out=/home/rose/cameras-1080p.xml\n\n"
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
    int return_val = 0;
    std::string calib_file; 
    int width;
    int height;
    unsigned int fps;

    std::string pipeline0, pipeline1;
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
    "{h help         |              | message }"
    "{width          |1920          | width }"
    "{height         |1080          | height }"
    "{fps            |30            | frame per second }"
    "{out            |cameras.xml   | output path and file name }";

    cv::CommandLineParser cmd_parser(argc, argv, keys);

    if (cmd_parser.has("help"))
    {
        help();
        goto cleanup;
    }

    calib_file = cmd_parser.get<std::string>("out"); 
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

    pipeline0 = create_capture(0, width, height, fps);
    capture0.open(pipeline0, cv::CAP_GSTREAMER);
    if (!capture0.isOpened())
    {
        std::cerr << pipeline0 << std::endl; 
        std::cerr << "Failed to open capture for first camera!" << std::endl;
        return_val = -4;
        goto cleanup;
    }

    pipeline1 = create_capture(1, width, height, fps);
    capture1.open(pipeline1, cv::CAP_GSTREAMER);
    if (!capture1.isOpened())
    {
        std::cerr << pipeline1 << std::endl; 
        std::cerr << "Failed to open capture for second camera!" << std::endl;
        return_val = -4;
        goto cleanup;
    }

    calib_settings.calib_file = calib_file; 
    calib_settings.image_size = cv::Size(width, height);
    calib_settings.match_mode = 0; 
    calib.reset(new videostitcher::CamerasCalib(calib_settings));
    if (!calib) {
        std::cerr << "Failed to start calibrator!" << std::endl;
        return_val = -5;
        goto cleanup;
    }

    cv::namedWindow(matches_window, cv::WINDOW_NORMAL); 
    cv::namedWindow(warping_window, cv::WINDOW_NORMAL); 

    cv::resizeWindow(matches_window, window_width, window_height);
    cv::resizeWindow(warping_window, window_width, window_height);

    cv::moveWindow(matches_window, 200, 100); 
    cv::moveWindow(warping_window, window_width + 250, 100); 

    unsigned long frame_count; 
    g_stop = false;
    signal(SIGINT, signal_callback_handler);
    while (!g_stop)
    {
        // std::cout << "frame " << frame_count++ << std::endl; 
        capture0 >> images[0];
        capture1 >> images[1]; 

        cuda_images[0].upload(images[0]); 
        cuda_images[1].upload(images[1]);

        calib->Feed(cuda_images); 
        calib->Matches(images, matches_image); 
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
    }

cleanup:
    capture0.release();
    capture1.release();
    cv::destroyAllWindows(); 
    return return_val;
}
