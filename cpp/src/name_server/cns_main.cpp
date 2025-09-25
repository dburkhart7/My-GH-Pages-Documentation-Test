#include "cns.hpp"
#include <argparse/argparse.hpp>
#include <iostream>
#include <string>
#include <quill/Backend.h>
#include <csignal>
#include <memory>

using namespace std;

// Global pointer to allow signal handler to access the server
static unique_ptr<CentralNameServer> g_server;
static bool g_running = true;

void signal_handler(int signum) {
    if (g_server) {
        cout << "\nReceived signal " << signum << ". Shutting down gracefully..." << endl;
        g_running = false;
        g_server.reset(); // This will call the destructor which logs the shutdown
    }
    exit(signum);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    quill::Backend::start();
    argparse::ArgumentParser program("cns");
    
    program.add_argument("-ip", "--ip-address")
        .help("IP address to bind to")
        .default_value(std::string("127.0.0.1"));
    
    program.add_argument("-mip", "--master-ip-address")
        .help("Master IP address to connect to")
        .default_value(std::string("127.0.0.1"));
        
    program.add_argument("-p", "--port")
        .help("Port to bind to")
        .default_value(5555)
        .scan<'i', int>();

    program.add_argument("-d", "--debug")
        .help("Debug mode")
        .default_value(false)
        .implicit_value(true);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    auto ip = program.get<std::string>("ip-address");
    auto mip = program.get<std::string>("master-ip-address");
    auto port = program.get<int>("port");
    auto debug = program.get<bool>("debug");

    try {
        g_server = make_unique<CentralNameServer>(ip, port, mip);
        if (debug) {
            g_server->set_debug(true); // Set debug mode
        }
        while (g_running) {
            try {
                g_server->reply_loop();
                return 0; // only gets here if reply_loop() returns
            } catch (const std::exception& e) {
                std::cerr << "Error in reply loop: " << e.what() << std::endl;
                break;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}