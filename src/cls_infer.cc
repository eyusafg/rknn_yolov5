// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define _BASETSD_H

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "postprocess.h"
#include "rga.h"
#include "rknn_api.h"

#include <iostream>

using namespace cv;
using namespace std;

#define PERF_WITH_POST 1
/*-------------------------------------------
                  Functions
-------------------------------------------*/

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
    std::string shape_str = attr->n_dims < 1 ? "" : std::to_string(attr->dims[0]);
    for (int i = 1; i < attr->n_dims; ++i)
    {
        shape_str += ", " + std::to_string(attr->dims[i]);
    }

    printf("  index=%d, name=%s, n_dims=%d, dims=[%s], n_elems=%d, size=%d, w_stride = %d, size_with_stride=%d, fmt=%s, "
           "type=%s, qnt_type=%s, "
           "zp=%d, scale=%f\n",
           attr->index, attr->name, attr->n_dims, shape_str.c_str(), attr->n_elems, attr->size, attr->w_stride,
           attr->size_with_stride, get_format_string(attr->fmt), get_type_string(attr->type),
           get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz)
{
    unsigned char *data;
    int ret;

    data = NULL;

    if (NULL == fp)
    {
        return NULL;
    }

    ret = fseek(fp, ofst, SEEK_SET);
    if (ret != 0)
    {
        printf("blob seek failure.\n");
        return NULL;
    }

    data = (unsigned char *)malloc(sz);
    if (data == NULL)
    {
        printf("buffer malloc failure.\n");
        return NULL;
    }
    ret = fread(data, 1, sz, fp);
    return data;
}

static unsigned char *load_model(const char *filename, int *model_size)
{
    FILE *fp;
    unsigned char *data;

    fp = fopen(filename, "rb");
    if (NULL == fp)
    {
        printf("Open file %s failed.\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);

    data = load_data(fp, 0, size);

    fclose(fp);

    *model_size = size;
    return data;
}

static int saveFloat(const char *file_name, float *output, int element_size)
{
    FILE *fp;
    fp = fopen(file_name, "w");
    for (int i = 0; i < element_size; i++)
    {
        fprintf(fp, "%.6f\n", output[i]);
    }
    fclose(fp);
    return 0;
}

void process(cv::Mat& image, cv::Mat& blob)
{   
    const int width = 640;
    const int heigh = 640;
    int crop_size = std::min(image.cols, image.rows);
    int left = (image.cols - crop_size) / 2, top = (image.rows - crop_size) / 2;
    cv::Mat crop_img = image(cv::Rect(left, top, crop_size, crop_size));
    cv::resize(crop_img, crop_img, cv::Size(640, 640));

    // 不进行resize 进行补0 norm
    
        

}


cv::Mat letterbox(cv::Mat &img, cv::Size new_shape, cv::Scalar color, bool _auto, bool scaleFill, bool scaleup, int stride)
{
    float width = img.cols;
    float height = img.rows;
    float r = min(new_shape.width / width, new_shape.height / height);
    if (!scaleup)
        r = min(r, 1.0f);
    int newpadW = int(round(width * r));
    int newpadH = int(round(height * r));
    int dw = new_shape.width - newpadW;
    int dh = new_shape.height - newpadH;
    if (_auto)
    {
        dw %= stride;
        dh %= stride;
    }

    dw /= 2;
    dh /= 2;
    cv::Mat dst;
    cv::resize(img, dst, cv::Size(newpadW
        , newpadH), 0, 0, cv::INTER_LINEAR);
    int top = int(round(dh - 0.1));
    int bottom = int(round(dh + 0.1));
    int left = int(round(dw - 0.1));
    int right = int(round(dw + 0.1));
    cv::copyMakeBorder(dst, dst, top, bottom, left, right, cv::BORDER_CONSTANT, color); // 填充常数 color填充颜色
    return dst;
}


void postprocess(const int8_t* output, const int num_outputs){

    std::vector<float> float_outputs(num_outputs);
    for (int i = 0; i < num_outputs; i++){
        float_outputs[i] = static_cast<float>(output[i]);

    }
    float sum = 0.0f;
    for (int i = 0; i < num_outputs; ++i){
        float_outputs[i] = std::exp(float_outputs[i]);
        sum += float_outputs[i];
    }

    for (int i = 0; i < num_outputs; ++i){
        float_outputs[i] /= sum;
    }

    for (int i = 0; i < num_outputs; ++i){
        std::cout << "Predicted : " << float_outputs[i] << std::endl;
    }
    


    int predicted_class = std::distance(float_outputs.begin(), std::max_element(float_outputs.begin(),
    float_outputs.end()));

    std::cout << "Predicted Class: " << predicted_class << std::endl;

}

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main(int argc, char **argv)
{
    int status = 0;
    char *model_name = NULL;
    rknn_context ctx;
    size_t actual_size = 0;
    int img_width = 0;
    int img_height = 0;
    int img_channel = 0;
    const float nms_threshold = NMS_THRESH;
    const float box_conf_threshold = BOX_THRESH;
    struct timeval start_time, stop_time;
    int ret;

    // init rga context
    rga_buffer_t src;
    rga_buffer_t dst;
    im_rect src_rect;
    im_rect dst_rect;
    memset(&src_rect, 0, sizeof(src_rect));
    memset(&dst_rect, 0, sizeof(dst_rect));
    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));

    if (argc != 3)
    {
        printf("Usage: %s <rknn model> <jpg> \n", argv[0]);
        return -1;
    }

    printf("post process config: box_conf_threshold = %.2f, nms_threshold = %.2f\n", box_conf_threshold, nms_threshold);

    model_name = (char *)argv[1];
    char *image_name = argv[2];

    printf("Read %s ...\n", image_name);
    cv::Mat orig_img = cv::imread(image_name, 1);
    if (!orig_img.data)
    {
        printf("cv::imread %s fail!\n", image_name);
        return -1;
    }
    cv::Mat img;
    cv::cvtColor(orig_img, img, cv::COLOR_BGR2RGB);
    img_width = img.cols;
    img_height = img.rows;
    printf("img width = %d, img height = %d\n", img_width, img_height);

    letterbox(img, cv::Size(640,640), cv::Scalar(0,0,0), false, false, false, 32);

    /* Create the neural network */
    printf("Loading mode...\n");
    int model_data_size = 0;
    unsigned char *model_data = load_model(model_name, &model_data_size);
    ret = rknn_init(&ctx, model_data, model_data_size, 0, NULL);
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }

    rknn_sdk_version version;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

    rknn_input_output_num io_num;
    ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0)
    {
        printf("rknn_init error ret=%d\n", ret);
        return -1;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    rknn_tensor_attr input_attrs[io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (int i = 0; i < io_num.n_input; i++)
    {
        input_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0)
        {
            printf("rknn_init error ret=%d\n", ret);
            return -1;
        }
        dump_tensor_attr(&(input_attrs[i]));
    }

    rknn_tensor_attr output_attrs[io_num.n_output];
    memset(output_attrs, 0, sizeof(output_attrs));
    for (int i = 0; i < io_num.n_output; i++)
    {
        output_attrs[i].index = i;
        ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        dump_tensor_attr(&(output_attrs[i]));

        std::cout << "output_attrs[" << i << "] TYPE: " << output_attrs[i].type << std::endl;
        std::cout << "output_attrs[" << i << "] shape: ";

        for (int j = 0; j < output_attrs[i].n_dims; ++j){
            std::cout << output_attrs[i].dims[j] << " ";
        }
        std::cout << std::endl;
    }



    int channel = 3;
    int width = 0;
    int height = 0;
    if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
    {
        printf("model is NCHW input fmt\n");
        channel = input_attrs[0].dims[1];
        height = input_attrs[0].dims[2];
        width = input_attrs[0].dims[3];
    }
    else
    {
        printf("model is NHWC input fmt\n");
        height = input_attrs[0].dims[1];
        width = input_attrs[0].dims[2];
        channel = input_attrs[0].dims[3];
    }

    printf("model input height=%d, width=%d, channel=%d\n", height, width, channel);

    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = width * height * channel;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;

    // You may not need resize when src resulotion equals to dst resulotion
    void *resize_buf = nullptr;

    if (img_width != width || img_height != height)
    {
        printf("resize with RGA!\n");
        resize_buf = malloc(height * width * channel);
        memset(resize_buf, 0x00, height * width * channel);

        src = wrapbuffer_virtualaddr((void *)img.data, img_width, img_height, RK_FORMAT_RGB_888);
        dst = wrapbuffer_virtualaddr((void *)resize_buf, width, height, RK_FORMAT_RGB_888);
        ret = imcheck(src, dst, src_rect, dst_rect);
        if (IM_STATUS_NOERROR != ret)
        {
            printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
            return -1;
        }

        IM_STATUS STATUS = imresize(src, dst);

        // for debug
        cv::Mat resize_img(cv::Size(width, height), CV_8UC3, resize_buf);
        cv::imwrite("resize_input.jpg", resize_img);

        inputs[0].buf = resize_buf;
    }
    else
    {
        inputs[0].buf = (void *)img.data;
    }

    gettimeofday(&start_time, NULL);
    rknn_inputs_set(ctx, io_num.n_input, inputs);

    rknn_output outputs[io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < io_num.n_output; i++)
    {
        outputs[i].want_float = 0;
    }

    ret = rknn_run(ctx, NULL);
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);
    gettimeofday(&stop_time, NULL);
    printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

    

    int8_t * itr = (int8_t *)outputs[0].buf;
    int8_t output_vec[2];
    output_vec[0] = *itr;
    itr++;
    output_vec[1] = *itr;

    // printf("model output[0]=%d, output[1]=%d\n", output_vec[0],output_vec[1]);


    // std::vector<int8_t> out_vec;
    // out_vec.clear();
    // out_vec.push_back(output_vec[0]);
    // out_vec.push_back(output_vec[1]);
    int num_outputs = 2;
    postprocess(output_vec, num_outputs);
    // std::vector<int8_t> a = softmax(out_vec);

    // printf("model result=%d,result1=%d\n", a[0],a[1]);


    // std::vector<double> outputs_vec = softmax(const std::vector<double>& input);
    // post process
//     float scale_w
//      = (float)width / img_width;
//     float scale_h = (float)height / img_height;

//     detect_result_group_t detect_result_group;
//     std::vector<float> out_scales;
//     std::vector<int32_t> out_zps;
//     for (int i = 0; i < io_num.n_output; ++i)
//     {
//         out_scales.push_back(output_attrs[i].scale);
//         out_zps.push_back(output_attrs[i].zp);
//     }
//     post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, height, width,
//                  box_conf_threshold, nms_threshold, scale_w, scale_h, out_zps, out_scales, &detect_result_group);

//     // Draw Objects
//     char text[256];
//     for (int i = 0; i < detect_result_group.count; i++)
//     {
//         detect_result_t *det_result = &(detect_result_group.results[i]);
//         sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
//         printf("%s @ (%d %d %d %d) %f\n", det_result->name, det_result->box.left, det_result->box.top,
//                det_result->box.right, det_result->box.bottom, det_result->prop);
//         int x1 = det_result->box.left;
//         int y1 = det_result->box.top;
//         int x2 = det_result->box.right;
//         int y2 = det_result->box.bottom;
//         rectangle(orig_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0, 255), 3);
//         putText(orig_img, text, cv::Point(x1, y1 + 12), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
//     }

//     imwrite("./out.jpg", orig_img);
//     ret = rknn_outputs_release(ctx, io_num.n_output, outputs);

//     // loop test
//     int test_count = 10;
//     gettimeofday(&start_time, NULL);
//     for (int i = 0; i < test_count; ++i)
//     {
//         rknn_inputs_set(ctx, io_num.n_input, inputs);
//         ret = rknn_run(ctx, NULL);
//         ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);
        
// #if PERF_WITH_POST
//         post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, height, width,
//                      box_conf_threshold, nms_threshold, scale_w, scale_h, out_zps, out_scales, &detect_result_group);
// #endif
//         ret = rknn_outputs_release(ctx, io_num.n_output, outputs);
//     }
//     gettimeofday(&stop_time, NULL);
//     printf("loop count = %d , average run  %f ms\n", test_count,
//            (__get_us(stop_time) - __get_us(start_time)) / 1000.0 / test_count);

//     deinitPostProcess();

//     // release
//     ret = rknn_destroy(ctx);

//     if (model_data)
//     {
//         free(model_data);
//     }

//     if (resize_buf)
//     {
//         free(resize_buf);
//     }

    return 0;
}
