/**
 * Azure Kinect IR Frame Producer (C++ version)
 *
 * This application reads IR frames from an Azure Kinect camera and publishes
 * them to a ZeroMQ socket. It's designed to test whether the C++ Azure Kinect SDK
 * can access the device over SSH where the Python version fails.
 */

#include <k4a/k4a.hpp>
#include <opencv2/opencv.hpp>
#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <string>
#include <chrono>
#include <nlohmann/json.hpp>
#include "../node.hpp"

// Configure logging
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/FileSink.h>
#include <quill/sinks/ConsoleSink.h>

// For convenience
using json = nlohmann::json;

// Constants (would typically be in a shared header)
const std::string CAMERA_TOPIC = "/camera/ir";
const int CAMERA_PORT = 5555;
const int EXPECTED_FRAME_RATE = 30;
const int MAX_CAP_FAIL_COUNT = 15;

// Global signal flag for clean shutdown
std::atomic<bool> g_stop_requested(false);
quill::Logger* g_logger = nullptr;

// Signal handler for clean shutdown
void signal_handler(int signal) {
    LOG_INFO(g_logger, "Received shutdown signal, stopping...");
    g_stop_requested = true;
}

class KinectAzureFrameProducer : public GenericNode {
public:
    KinectAzureFrameProducer(
        const std::string& topic = CAMERA_TOPIC,
        int port = CAMERA_PORT,
        uint32_t device_index = 0,
        uint32_t frame_drop = 0,
        bool master = false,
        bool save_images = false,
        quill::Logger* logger = nullptr
    ) : GenericNode("KinectFrameProducer", "KinectFrameProducer", "127.0.0.1", "127.0.0.1"),
        device_index_(device_index),
        frame_drop_(frame_drop),
        save_images_(save_images) {
        
        m_kinect_topic = m_topic + "/kinect";
        LOG_INFO(m_logger, "Kinect producer publishing to a random port with topic {}", m_kinect_topic);
        
        // Initialize ZMQ
        socket_ = setup_publisher({m_kinect_topic});
        
        // Configure Kinect
        k4a_device_configuration_t config = {
            K4A_IMAGE_FORMAT_COLOR_BGRA32,                 // color_format
            K4A_COLOR_RESOLUTION_720P,                     // color_resolution
            K4A_DEPTH_MODE_WFOV_2X2BINNED,                  // depth_mode
            K4A_FRAMES_PER_SECOND_30,                      // camera_fps
            true,                                          // synchronized_images_only
            0,                                             // depth_delay_off_color_usec
            K4A_WIRED_SYNC_MODE_STANDALONE,                // wired_sync_mode
            0,                                             // subordinate delay off master usec
        };
        if (master)
            config.wired_sync_mode = K4A_WIRED_SYNC_MODE_MASTER;
        else
            config.wired_sync_mode = K4A_WIRED_SYNC_MODE_STANDALONE;
        
        // Log device configuration
        LOG_INFO(m_logger, "Device configuration:");
        LOG_INFO(m_logger, "  depth_mode: {}", static_cast<int>(config.depth_mode));
        LOG_INFO(m_logger, "  camera_fps: {}", static_cast<int>(config.camera_fps));
        LOG_INFO(m_logger, "  wired_sync_mode: {}", static_cast<int>(config.wired_sync_mode));
        
        // Open the device
        LOG_INFO(m_logger, "Opening K4A device {}", device_index);
        try {
            device_ = k4a::device::open(device_index);
            
            // Start cameras
            device_.start_cameras(&config);
        } catch (const k4a::error& e) {
            throw std::runtime_error(std::string("Error: K4A device setup failed: ") + e.what());
        }
    }
    
    ~KinectAzureFrameProducer() {
        try {
            LOG_INFO(m_logger, "Closing device...");
            if (device_) {
                device_.stop_cameras();
                device_.close();
            }
            LOG_INFO(m_logger, "Device closed.");
        } catch (const std::exception& e) {
            LOG_ERROR(m_logger, "Error closing device: {}", e.what());
        }
    }
    
    void start() {
        LOG_INFO(m_logger, "Starting capture thread");
        running_ = true;
        capture_thread_ = std::thread(&KinectAzureFrameProducer::capture_loop, this);
    }
    
    void stop() {
        LOG_INFO(m_logger, "Stopping producer...");
        running_ = false;
        
        if (capture_thread_.joinable()) {
            LOG_DEBUG(m_logger, "Waiting for capture thread to join...");
            capture_thread_.join();
            LOG_DEBUG(m_logger, "Capture thread joined.");
        }
    }
    
private:
    std::string m_kinect_topic;
    uint32_t device_index_;
    uint32_t frame_drop_;
    bool save_images_;
    k4a::device device_;
    std::atomic<bool> running_{false};
    std::thread capture_thread_;
    unique_ptr<zmq::socket_t> socket_;
    
    void capture_loop() {
        uint64_t last_timestamp = 0;
        std::chrono::milliseconds timeout(200);
        int capture_fail_count = 0;

        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
        clahe->setClipLimit(4);
        clahe->setTilesGridSize(cv::Size(4, 4)); // Smaller tile size for faster processing
        
        try {
            while (running_ && !g_stop_requested && capture_fail_count < MAX_CAP_FAIL_COUNT) {
                // Skip frames if requested
                for (uint32_t i = 0; i < frame_drop_; i++) {
                    k4a::capture skipped_capture;
                    try {
                        device_.get_capture(&skipped_capture, std::chrono::milliseconds(K4A_WAIT_INFINITE));
                    } catch (const k4a::error&) {
                        // Ignore errors when skipping frames
                    }
                }
                
                // Get capture
                k4a::capture capture;
                try {
                    if (!device_.get_capture(&capture, timeout)) {
                        LOG_ERROR(m_logger, "Timed out getting capture");
                        capture_fail_count++;
                        continue;
                    }
                } catch (const k4a::error& e) {
                    LOG_ERROR(m_logger, "Error getting capture: {}", e.what());
                    continue;
                }
                capture_fail_count = 0;
                
                // Get IR image
                k4a::image ir_image = capture.get_ir_image();
                auto ts = std::chrono::system_clock::now();
                auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ts.time_since_epoch()).count();
                if (!ir_image) {
                    LOG_DEBUG(m_logger, "No IR image in capture");
                    continue;
                }
                
                // Get image data
                uint64_t device_timestamp = ir_image.get_device_timestamp().count();
                uint8_t* buffer = ir_image.get_buffer();
                size_t buffer_size = ir_image.get_size();
                int width = ir_image.get_width_pixels();
                int height = ir_image.get_height_pixels();
                
                // Convert to OpenCV matrix (16-bit)
                cv::Mat ir_mat(height, width, CV_16UC1, buffer);
                

                cv::Mat ir_processed;
                cv::threshold(ir_mat, ir_processed, 3000, 3000, cv::THRESH_TRUNC);
                ir_processed.convertTo(ir_processed, CV_8UC1, 255.0/3000.0);

                auto now_ts = std::chrono::system_clock::now();
                clahe->apply(ir_processed, ir_processed);
                auto ts_clahe = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - now_ts).count();
                LOG_DEBUG(m_logger, "Clahe: {} ms", ts_clahe);

                // Get RGB image (save it if enabled with --save flag)
                k4a::image rgb_image = capture.get_color_image();
                if (rgb_image) {
                    // send rgb image
                    std::string rgb_topic = "/camera/rgb";
                    zmq::message_t rgb_topic_msg(rgb_topic.size());
                    memcpy(rgb_topic_msg.data(), rgb_topic.data(), rgb_topic.size());
                    socket_->send(rgb_topic_msg, zmq::send_flags::sndmore);
                    
                    // Create metadata for rgb image (8-bit depth)
                    nlohmann::json rgb_metadata;
                    rgb_metadata["width"] = rgb_image.get_width_pixels();
                    rgb_metadata["height"] = rgb_image.get_height_pixels();
                    rgb_metadata["source_ts"] = ts_ms;
                    rgb_metadata["channels"] = 3;
                    rgb_metadata["bit_depth"] = 8;
                    rgb_metadata["device_timestamp"] = device_timestamp;
                    
                    // Send rgb metadata
                    std::string rgb_metadata_str = rgb_metadata.dump();
                    zmq::message_t rgb_metadata_msg(rgb_metadata_str.size());
                    memcpy(rgb_metadata_msg.data(), rgb_metadata_str.data(), rgb_metadata_str.size());
                    socket_->send(rgb_metadata_msg, zmq::send_flags::sndmore);
                    
                    // First create a BGR buffer (3 channels)
                    cv::Mat bgra_mat(rgb_image.get_height_pixels(), rgb_image.get_width_pixels(), CV_8UC4, rgb_image.get_buffer());
                    cv::Mat bgr_mat;
                    cv::cvtColor(bgra_mat, bgr_mat, cv::COLOR_BGRA2BGR);  // Remove alpha channel

                    // Then send the BGR data
                    size_t buffer_size_rgb = bgr_mat.total() * bgr_mat.elemSize();
                    zmq::message_t rgb_image_msg(buffer_size_rgb);
                    memcpy(rgb_image_msg.data(), bgr_mat.data, buffer_size_rgb);
                    socket_->send(rgb_image_msg);
                }
                
                // Prepare metadata
                json metadata;
                metadata["device_timestamp"] = device_timestamp;
                metadata["source_ts"] = ts_ms;
                metadata["width"] = width;
                metadata["height"] = height;
                metadata["channels"] = 1;
                metadata["bit_depth"] = 8; // 8-bit grayscale
                
                // Send topic
                zmq::message_t topic_msg(m_kinect_topic.size());
                memcpy(topic_msg.data(), m_kinect_topic.data(), m_kinect_topic.size());
                socket_->send(topic_msg, zmq::send_flags::sndmore);
                
                // Send metadata
                std::string metadata_str = metadata.dump();
                zmq::message_t metadata_msg(metadata_str.size());
                memcpy(metadata_msg.data(), metadata_str.data(), metadata_str.size());
                socket_->send(metadata_msg, zmq::send_flags::sndmore);
                
                // Send image data
                zmq::message_t image_msg(ir_processed.total() * ir_processed.elemSize());
                memcpy(image_msg.data(), ir_processed.data, ir_processed.total() * ir_processed.elemSize());
                socket_->send(image_msg);
                
                // Send raw IR data
                {
                    // Create a new topic for raw data
                    std::string raw_topic = "/camera/raw_ir";
                    zmq::message_t raw_topic_msg(raw_topic.size());
                    memcpy(raw_topic_msg.data(), raw_topic.data(), raw_topic.size());
                    socket_->send(raw_topic_msg, zmq::send_flags::sndmore);
                    
                    // Create metadata for raw IR image (16-bit depth)
                    nlohmann::json raw_metadata;
                    raw_metadata["width"] = width;
                    raw_metadata["height"] = height;
                    raw_metadata["source_ts"] = ts_ms;
                    raw_metadata["channels"] = 1;
                    raw_metadata["bit_depth"] = 16;
                    raw_metadata["device_timestamp"] = device_timestamp;
                    
                    // Send raw metadata
                    std::string raw_metadata_str = raw_metadata.dump();
                    zmq::message_t raw_metadata_msg(raw_metadata_str.size());
                    memcpy(raw_metadata_msg.data(), raw_metadata_str.data(), raw_metadata_str.size());
                    socket_->send(raw_metadata_msg, zmq::send_flags::sndmore);
                    
                    // Send raw image data directly from the depth engine buffer
                    uint8_t* buffer_raw = ir_image.get_buffer();
                    size_t buffer_size_raw = ir_image.get_size();
                    zmq::message_t raw_image_msg(buffer_size_raw);
                    memcpy(raw_image_msg.data(), buffer_raw, buffer_size_raw);
                    socket_->send(raw_image_msg);
                }
                
                // Calculate frame time
                if (last_timestamp > 0) {
                    double diff = (device_timestamp - last_timestamp) / 1000.0;
                    double max_frame_time = (1000.0 / (EXPECTED_FRAME_RATE - 2)) * (frame_drop_ + 1);
                    if (diff > max_frame_time) {
                        LOG_WARNING(m_logger, "Frame capture slow: {:.3f} ms > {:.1f}", diff, max_frame_time);
                    } else {
                        LOG_DEBUG(m_logger, "Captured frame in {:.3f} ms", diff);
                    }
                }
                last_timestamp = device_timestamp;
            } // End of while loop
        } catch (const std::exception& e) {
            LOG_ERROR(m_logger, "Exception in capture loop: {}", e.what());
        }
        
        g_stop_requested = true;
        LOG_INFO(m_logger, "Capture loop terminated");
    }
};

int main(int argc, char** argv) {
    // Set up logging
    quill::Backend::start();
    std::string logFilePath = "./logs/kinect.log";
    quill::FileSinkConfig file_sink_config;
    file_sink_config.set_open_mode('a');
    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(logFilePath, file_sink_config);
    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
    g_logger = quill::Frontend::create_or_get_logger("KinectAzure", {std::move(file_sink), std::move(console_sink)});
    g_logger->set_log_level(quill::LogLevel::Info);
    
    // Parse command line arguments
    uint32_t device_index = 0;
    uint32_t frame_drop = 0;
    std::string topic = CAMERA_TOPIC;
    bool verbose = false;
    bool save_images = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--device-index" && i + 1 < argc) {
            device_index = std::stoi(argv[++i]);
        } else if (arg == "--frame-drop" && i + 1 < argc) {
            frame_drop = std::stoi(argv[++i]);
        } else if (arg == "--topic" && i + 1 < argc) {
            topic = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
            g_logger->set_log_level(quill::LogLevel::Debug);
        } else if (arg == "--save") {
            save_images = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --device-index INDEX  Index of the Kinect device to open (default: 0)" << std::endl;
            std::cout << "  --frame-drop COUNT    Number of frames to drop (default: 0)" << std::endl;
            std::cout << "  --topic TOPIC         ZMQ topic to publish frames to (default: " << CAMERA_TOPIC << ")" << std::endl;
            std::cout << "  --verbose, -v         Enable verbose debug logging" << std::endl;
            std::cout << "  --save                Save RGB images to disk" << std::endl;
            std::cout << "  --help, -h            Show this help message" << std::endl;
            return 0;
        }
    }
    
    // Set up signal handling
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    LOG_INFO(g_logger, "Starting IR frame producer with:");
    LOG_INFO(g_logger, "  Output: tcp://*:{} (topic: {})", CAMERA_PORT, topic);
    LOG_INFO(g_logger, "  Device: {}", device_index);
    LOG_INFO(g_logger, "  Save RGB images: {}", save_images ? "enabled" : "disabled");

    try {
        // Create and start producer
        KinectAzureFrameProducer producer(topic, CAMERA_PORT, device_index, frame_drop, false, save_images);
        producer.start();
        
        LOG_INFO(g_logger, "Press Ctrl+C to stop");
        
        // Main loop - watch for shutdown flag
        while (!g_stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO(g_logger, "Shutting down...");
        producer.stop();
    } catch (const std::exception& e) {
        LOG_ERROR(g_logger, "Error: {}", e.what());
        return 1;
    }
    
    LOG_INFO(g_logger, "Cleanup complete, exiting.");
    return 0;
}
