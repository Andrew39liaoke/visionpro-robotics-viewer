#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <librealsense2/rs.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
}

struct Options {
    int width = 1280;
    int height = 720;
    int fps = 30;
    int bitrate_kbps = 5000;
    std::string encoder = "auto";
    std::string serial;
    std::string rtsp_url = "rtsp://127.0.0.1:8554/d435i";
    bool list_encoders = false;
    bool help = false;
};

struct EncoderCandidate {
    std::string name;
    std::string pipeline_fragment;
    bool hardware;
};

std::string quote_gst(const std::string& value) {
    std::string result = "\"";
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

int parse_positive_int(const std::string& name, const std::string& value) {
    std::size_t consumed = 0;
    int parsed = 0;
    try {
        parsed = std::stoi(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(name + " 必须是正整数，实际值: " + value);
    }
    if (consumed != value.size() || parsed <= 0) {
        throw std::runtime_error(name + " 必须是正整数，实际值: " + value);
    }
    return parsed;
}

std::string take_value(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        throw std::runtime_error(option + " 缺少参数值");
    }
    return argv[++index];
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--width") {
            options.width = parse_positive_int(arg, take_value(i, argc, argv, arg));
        } else if (arg == "--height") {
            options.height = parse_positive_int(arg, take_value(i, argc, argv, arg));
        } else if (arg == "--fps") {
            options.fps = parse_positive_int(arg, take_value(i, argc, argv, arg));
        } else if (arg == "--bitrate-kbps") {
            options.bitrate_kbps = parse_positive_int(arg, take_value(i, argc, argv, arg));
        } else if (arg == "--encoder") {
            options.encoder = take_value(i, argc, argv, arg);
        } else if (arg == "--serial") {
            options.serial = take_value(i, argc, argv, arg);
        } else if (arg == "--rtsp-url") {
            options.rtsp_url = take_value(i, argc, argv, arg);
        } else if (arg == "--list-encoders") {
            options.list_encoders = true;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else {
            throw std::runtime_error("未知参数: " + arg);
        }
    }
    return options;
}

void print_help(const char* program) {
    std::cout
        << "D435i RGB -> NV12 -> H.264 -> RTSP/MediaMTX -> WebRTC\n\n"
        << "用法:\n  " << program << " [选项]\n\n"
        << "选项:\n"
        << "  --width N             彩色视频宽度，默认 1280\n"
        << "  --height N            彩色视频高度，默认 720\n"
        << "  --fps N               帧率，默认 30\n"
        << "  --bitrate-kbps N      H.264 目标码率，默认 5000\n"
        << "  --encoder NAME        auto、nvh264enc、qsvh264enc、vaapih264enc 或 x264enc\n"
        << "  --serial SERIAL       多相机时指定 RealSense 序列号\n"
        << "  --rtsp-url URL        发布地址，默认 rtsp://127.0.0.1:8554/d435i\n"
        << "  --list-encoders       显示本机可用编码器后退出\n"
        << "  -h, --help            显示帮助\n";
}

bool element_exists(const std::string& name) {
    GstElementFactory* factory = gst_element_factory_find(name.c_str());
    if (factory == nullptr) {
        return false;
    }
    // The registry can still contain a factory whose shared library can no
    // longer be loaded (for example, an NVIDIA plugin without a working
    // driver). Creating a temporary element verifies that the plugin is
    // actually usable in this process.
    GstElement* element = gst_element_factory_create(factory, nullptr);
    gst_object_unref(factory);
    if (element == nullptr) {
        return false;
    }
    gst_object_unref(element);
    return true;
}

std::vector<EncoderCandidate> encoder_candidates(int bitrate_kbps, int fps) {
    const std::string bitrate = std::to_string(bitrate_kbps);
    const std::string bitrate_bps = std::to_string(static_cast<std::int64_t>(bitrate_kbps) * 1000);
    const std::string gop = std::to_string(fps);

    return {
        {
            "nvh264enc",
            "nvh264enc preset=low-latency-hq zerolatency=true rc-mode=cbr "
            "bitrate=" + bitrate + " gop-size=" + gop + " bframes=0",
            true,
        },
        {
            "qsvh264enc",
            "qsvh264enc rate-control=cbr bitrate=" + bitrate +
            " gop-size=" + gop + " b-frames=0 low-latency=true",
            true,
        },
        {
            "vaapih264enc",
            "vaapih264enc rate-control=cbr bitrate=" + bitrate +
            " keyframe-period=" + gop + " max-bframes=0 quality-level=1",
            true,
        },
        {
            "x264enc",
            "x264enc tune=zerolatency speed-preset=ultrafast bitrate=" + bitrate +
            " key-int-max=" + gop + " bframes=0 byte-stream=true aud=true sliced-threads=true",
            false,
        },
        {
            "openh264enc",
            "openh264enc bitrate=" + bitrate_bps + " gop-size=" + gop +
            " rate-control=bitrate complexity=low enable-frame-skip=true",
            false,
        },
    };
}

void print_encoders(int bitrate_kbps, int fps) {
    std::cout << "GStreamer H.264 编码器:\n";
    for (const auto& candidate : encoder_candidates(bitrate_kbps, fps)) {
        std::cout << "  " << std::left << std::setw(14) << candidate.name
                  << (element_exists(candidate.name) ? "可用" : "不可用")
                  << (candidate.hardware ? "（硬件）" : "（软件）") << '\n';
    }
    std::cout << "  rtspclientsink  "
              << (element_exists("rtspclientsink") ? "可用" : "不可用") << '\n';
}

EncoderCandidate choose_encoder(const Options& options) {
    const auto candidates = encoder_candidates(options.bitrate_kbps, options.fps);
    if (options.encoder != "auto") {
        for (const auto& candidate : candidates) {
            if (candidate.name == options.encoder) {
                if (!element_exists(candidate.name)) {
                    throw std::runtime_error(
                        "指定的编码器 " + candidate.name + " 未安装。请运行 --list-encoders 查看可用项。"
                    );
                }
                return candidate;
            }
        }
        throw std::runtime_error("不支持的编码器: " + options.encoder);
    }

    for (const auto& candidate : candidates) {
        if (element_exists(candidate.name)) {
            return candidate;
        }
    }
    throw std::runtime_error("没有可用的 H.264 编码器，请安装 GStreamer ugly/bad 插件。");
}

std::string build_pipeline_description(const Options& options, const EncoderCandidate& encoder) {
    std::ostringstream pipeline;
    pipeline
        << "appsrc name=source is-live=true format=time do-timestamp=true block=false "
        << "caps=video/x-raw,format=RGB,width=" << options.width
        << ",height=" << options.height
        << ",framerate=" << options.fps << "/1 "
        << "! queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream "
        << "! videoconvert n-threads=2 "
        << "! video/x-raw,format=NV12 "
        << "! " << encoder.pipeline_fragment << ' '
        << "! video/x-h264,profile=baseline "
        << "! h264parse config-interval=-1 "
        << "! rtspclientsink location=" << quote_gst(options.rtsp_url)
        << " protocols=tcp latency=0";
    return pipeline.str();
}

class GstPipelineGuard {
public:
    explicit GstPipelineGuard(GstElement* pipeline) : pipeline_(pipeline) {}
    ~GstPipelineGuard() {
        if (pipeline_ != nullptr) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
        }
    }
    GstPipelineGuard(const GstPipelineGuard&) = delete;
    GstPipelineGuard& operator=(const GstPipelineGuard&) = delete;

private:
    GstElement* pipeline_;
};

bool print_bus_error(GstBus* bus) {
    GstMessage* message = gst_bus_pop_filtered(
        bus,
        static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)
    );
    if (message == nullptr) {
        return false;
    }

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        std::cerr << "\nGStreamer 错误: " << (error ? error->message : "未知错误") << '\n';
        if (debug != nullptr) {
            std::cerr << "调试信息: " << debug << '\n';
        }
        g_clear_error(&error);
        g_free(debug);
    } else {
        std::cerr << "\nGStreamer 管线已结束。\n";
    }
    gst_message_unref(message);
    return true;
}

int run(const Options& options) {
    if (!element_exists("rtspclientsink")) {
        throw std::runtime_error(
            "缺少 GStreamer rtspclientsink。Ubuntu 请安装 gstreamer1.0-rtsp。"
        );
    }

    const EncoderCandidate encoder = choose_encoder(options);
    const std::string pipeline_description = build_pipeline_description(options, encoder);

    GError* parse_error = nullptr;
    GstElement* pipeline = gst_parse_launch(pipeline_description.c_str(), &parse_error);
    if (pipeline == nullptr || parse_error != nullptr) {
        const std::string message = parse_error ? parse_error->message : "未知解析错误";
        g_clear_error(&parse_error);
        if (pipeline != nullptr) {
            gst_object_unref(pipeline);
        }
        throw std::runtime_error("创建 GStreamer 管线失败: " + message +
                                 "\n管线: " + pipeline_description);
    }
    GstPipelineGuard pipeline_guard(pipeline);

    GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    if (source == nullptr) {
        throw std::runtime_error("无法获取 GStreamer appsrc 元素");
    }

    GstBus* bus = gst_element_get_bus(pipeline);
    if (bus == nullptr) {
        gst_object_unref(source);
        throw std::runtime_error("无法获取 GStreamer bus");
    }

    rs2::pipeline camera_pipeline;
    rs2::config config;
    if (!options.serial.empty()) {
        config.enable_device(options.serial);
    }
    config.enable_stream(
        RS2_STREAM_COLOR,
        options.width,
        options.height,
        RS2_FORMAT_RGB8,
        options.fps
    );

    std::cout << "正在启动 D435i 彩色相机...\n";
    rs2::pipeline_profile profile = camera_pipeline.start(config);
    const rs2::device device = profile.get_device();
    std::cout << "设备: " << device.get_info(RS2_CAMERA_INFO_NAME)
              << "，序列号: " << device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) << '\n';

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        camera_pipeline.stop();
        gst_object_unref(bus);
        gst_object_unref(source);
        throw std::runtime_error("GStreamer 管线无法进入 PLAYING 状态");
    }

    std::cout << "编码器: " << encoder.name
              << (encoder.hardware ? "（硬件）" : "（软件回退）") << '\n'
              << "输入: RGB8 " << options.width << 'x' << options.height << '@' << options.fps << '\n'
              << "中间格式: NV12\n"
              << "发布地址: " << options.rtsp_url << '\n'
              << "按 Ctrl+C 停止。\n";

    std::uint64_t captured = 0;
    std::uint64_t pushed = 0;
    std::uint64_t rejected = 0;
    auto report_start = std::chrono::steady_clock::now();
    std::uint64_t report_frames = 0;

    try {
        while (g_running != 0) {
            rs2::frameset frames;
            if (!camera_pipeline.poll_for_frames(&frames)) {
                if (print_bus_error(bus)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            const rs2::video_frame color = frames.get_color_frame();
            if (!color) {
                continue;
            }
            ++captured;

            const std::size_t bytes = color.get_data_size();
            GstBuffer* buffer = gst_buffer_new_allocate(nullptr, bytes, nullptr);
            if (buffer == nullptr) {
                throw std::runtime_error("无法分配 GStreamer 视频缓冲区");
            }
            gst_buffer_fill(buffer, 0, color.get_data(), bytes);
            GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, options.fps);

            const GstFlowReturn flow = gst_app_src_push_buffer(GST_APP_SRC(source), buffer);
            if (flow == GST_FLOW_OK) {
                ++pushed;
                ++report_frames;
            } else {
                ++rejected;
                if (flow == GST_FLOW_FLUSHING || flow == GST_FLOW_EOS) {
                    print_bus_error(bus);
                    break;
                }
            }

            if (print_bus_error(bus)) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - report_start).count();
            if (seconds >= 2.0) {
                std::cout << "\r采集=" << captured
                          << " 推送=" << pushed
                          << " 拒绝=" << rejected
                          << " 当前=" << std::fixed << std::setprecision(1)
                          << static_cast<double>(report_frames) / seconds << " fps" << std::flush;
                report_start = now;
                report_frames = 0;
            }
        }
    } catch (...) {
        camera_pipeline.stop();
        gst_app_src_end_of_stream(GST_APP_SRC(source));
        gst_object_unref(bus);
        gst_object_unref(source);
        throw;
    }

    camera_pipeline.stop();
    gst_app_src_end_of_stream(GST_APP_SRC(source));
    gst_element_get_state(pipeline, nullptr, nullptr, 500 * GST_MSECOND);
    gst_object_unref(bus);
    gst_object_unref(source);

    std::cout << "\n已停止。采集=" << captured
              << "，推送=" << pushed
              << "，拒绝=" << rejected << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        const Options options = parse_options(argc, argv);
        if (options.help) {
            print_help(argv[0]);
            return 0;
        }
        if (options.list_encoders) {
            print_encoders(options.bitrate_kbps, options.fps);
            return 0;
        }
        return run(options);
    } catch (const rs2::error& error) {
        std::cerr << "RealSense 错误: " << error.what() << '\n'
                  << "调用: " << error.get_failed_function() << '(' << error.get_failed_args() << ")\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "错误: " << error.what() << '\n';
        return 1;
    }
}
