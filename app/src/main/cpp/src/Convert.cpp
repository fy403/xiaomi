//#include "Convert.h"
//#include "Log.h"
//#include <fstream>
//#include <string>
//#include <filesystem> // 添加文件系统库
//
//Converter::Converter(const std::string &output_path) : sws_ctx(nullptr) {
//    // 检查文件是否存在，如果存在则删除
//    if (std::__fs::filesystem::exists(output_path)) {
//        std::__fs::filesystem::remove(output_path);
//    }
//
//    // 打开输出文件
//    output_file.open(output_path, std::ios::binary);
//    if (!output_file.is_open()) {
//        LOGE("Could not open output file: %s", output_path.c_str());
//        throw std::runtime_error("Failed to open output file");
//    }
//    LOGI("Output file opened successfully: %s", output_path.c_str());
//}
//
//Converter::~Converter() {
//    // 释放资源并关闭文件
//    if (sws_ctx) {
//        sws_freeContext(sws_ctx);
//        sws_ctx = nullptr;
//    }
//    if (output_file.is_open()) {
//        output_file.close();
//    }
//}
//
//bool Converter::convertAndStoreFrame(AVFrame *frame) {
//    if (!frame) {
//        LOGE("Invalid input parameters");
//        return false;
//    }
//
//    int width = frame->width;
//    int height = frame->height;
//
//    // 转换为YUV420P格式
//    AVFrame *frame_yuv420p = av_frame_alloc();
//    if (!frame_yuv420p) {
//        LOGE("Failed to allocate AVFrame");
//        return false;
//    }
//
//    frame_yuv420p->format = AV_PIX_FMT_YUV420P;
//    frame_yuv420p->width = width;
//    frame_yuv420p->height = height;
//
//    int ret = av_image_alloc(frame_yuv420p->data, frame_yuv420p->linesize,
//                             width, height, AV_PIX_FMT_YUV420P, 32);
//    if (ret < 0) {
//        LOGE("Could not allocate raw picture buffer");
//        av_frame_free(&frame_yuv420p);
//        return false;
//    }
//
//    sws_ctx = sws_getContext(
//            frame->width, frame->height, (AVPixelFormat)frame->format,
//            frame->width, frame->height, AV_PIX_FMT_YUV420P,
//            SWS_BILINEAR, nullptr, nullptr, nullptr);
//
//    if (!sws_ctx) {
//        LOGE("Failed to create SwsContext");
//        av_frame_free(&frame_yuv420p);
//        return false;
//    }
//
//    sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize,
//              0, frame->height, frame_yuv420p->data, frame_yuv420p->linesize);
//
//    // 写入Y分量(亮度)
//    for (int y = 0; y < frame_yuv420p->height; y++) {
//        output_file.write(reinterpret_cast<char*>(frame_yuv420p->data[0] + y * frame_yuv420p->linesize[0]), width);
//    }
//
//    // 写入U分量(色度)
//    for (int y = 0; y < frame_yuv420p->height / 2; y++) {
//        output_file.write(reinterpret_cast<char*>(frame_yuv420p->data[1] + y * frame_yuv420p->linesize[1]), width / 2);
//    }
//
//    // 写入V分量(色度)
//    for (int y = 0; y < frame_yuv420p->height / 2; y++) {
//        output_file.write(reinterpret_cast<char*>(frame_yuv420p->data[2] + y * frame_yuv420p->linesize[2]), width / 2);
//    }
//
//    // 释放资源
//    sws_freeContext(sws_ctx);
//    sws_ctx = nullptr;
//    av_frame_free(&frame_yuv420p);
//
//    return true;
//}