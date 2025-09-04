//#ifndef CONVERT_H
//#define CONVERT_H
//#include <stdint.h>
//#include <fstream>
//
//extern "C" {
//    #include <libavformat/avformat.h>
//    #include <libavcodec/avcodec.h>
//    #include <libavutil/imgutils.h>
//    #include <libswscale/swscale.h>
//}
//
//
//class Converter {
//public:
//    Converter(const std::string &output_path);
//    ~Converter();
//    bool convertAndStoreFrame(AVFrame *frame);
//
//private:
//    SwsContext *sws_ctx;
//    std::ofstream output_file;
//};
//
//#endif // CONVERT_H