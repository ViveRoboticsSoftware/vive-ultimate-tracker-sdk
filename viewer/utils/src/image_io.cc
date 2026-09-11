
#include "../include/image_io.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace vive {

// free after use
uint8_t* read_image(char const* filename, int& width, int& height, int& channels) {
  if (filename) {
    cv::Mat map = cv::imread(filename, cv::IMREAD_UNCHANGED);
    if (!map.empty() && CV_8U==map.depth()) {
      width = map.cols;
      height = map.rows;
      channels = map.channels();
      if (width>0 && height>0 && (1==channels || 3==channels || 4==channels)) {
        int const pixel_size = width*height*channels;
        if (uint8_t* pixels = (uint8_t*) malloc(pixel_size)) {
          if (1==channels) {
            memcpy(pixels, map.data, pixel_size);
          } else if (3==channels) {
            cv::Mat rgb(height, width, CV_8UC3, pixels);
            cv::cvtColor(map, rgb, cv::COLOR_BGR2RGB);
          } else if (4==channels) {
            cv::Mat rgba(height, width, CV_8UC4, pixels);
            cv::cvtColor(map, rgba, cv::COLOR_BGRA2RGBA);
          }
          return pixels;
        }
      }
    }
  }
  return nullptr;
}

bool write_image(char const* filename, void const* pixels, int width, int height, int channels) {
  if (filename) {
    if (1==channels) {
      return cv::imwrite(filename, cv::Mat(height, width, CV_8UC1, (void*) pixels));
    } else if (3==channels) {
      cv::Mat map;
      cv::cvtColor(cv::Mat(height, width, CV_8UC3, (void*) pixels), map, cv::COLOR_BGR2RGB);
      return cv::imwrite(filename, map);
    } else if (4==channels) {
      cv::Mat map;
      cv::cvtColor(cv::Mat(height, width, CV_8UC4, (void*) pixels), map, cv::COLOR_BGRA2RGBA);
      return cv::imwrite(filename, map);
    }
  }
  return false;
}

}
