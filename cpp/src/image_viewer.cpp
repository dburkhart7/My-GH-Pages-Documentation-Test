#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include "node.hpp"

using json = nlohmann::json;

class ImageViewer : public GenericNode {
public:
    ImageViewer() : GenericNode("ImageViewer", "ImageViewer", "127.0.0.1", "127.0.0.1") {
        // Optionally, name your window
        cv::namedWindow("Image Viewer", cv::WINDOW_AUTOSIZE);
        socket_ = setup_subscriber("/KinectFrameProducer/KinectFrameProducer/kinect");
    }

    void run() {
        while (true) {
            zmq::message_t topic_msg;
            zmq::message_t metadata_msg;
            zmq::message_t image_msg;

            // Receive topic
            if (!socket_->recv(topic_msg, zmq::recv_flags::none)) {
                cout<<"no topic"<<endl;
                LOG_ERROR(m_logger, "Failed to receive topic");
                continue;
            }
            std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());

            //Receive metadata
            if (!socket_->recv(metadata_msg, zmq::recv_flags::none)) {
                cout<<"no metadata"<<endl;
                LOG_ERROR(m_logger, "Failed to receive metadata");
                continue;
            }
            std::string msg_str(static_cast<char*>(metadata_msg.data()), metadata_msg.size());
            json metadata = json::parse(msg_str);

            // Receive image buffer
            if (!socket_->recv(image_msg, zmq::recv_flags::none)) {
                cout<<"no image"<<endl;
                LOG_ERROR(m_logger, "Failed to receive image");
                continue;
            }

            cv::Mat img(metadata["height"], metadata["width"], CV_8UC1, image_msg.data());
            if (img.empty()) {
                cout<<"bad buffer"<<endl;
                LOG_ERROR(m_logger, "Failed to decode buffer");
                continue;
            }

            cv::imshow("Image Viewer", img);  // Display image
            cv::waitKey(1);
        }

        cv::destroyAllWindows();
    }

private:
    unique_ptr<zmq::socket_t> socket_;
};

int main() {
    ImageViewer viewer = ImageViewer();
    viewer.run();
    return 0;
}
