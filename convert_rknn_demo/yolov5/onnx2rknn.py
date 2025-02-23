
import cv2
import numpy as np

from rknn.api import RKNN
import os

if __name__ == '__main__':

    platform = 'rk3588'
    exp = '5s_cls'
    Width = 640
    Height = 640
    MODEL_PATH = './best.onnx'
    NEED_BUILD_MODEL = True
    # NEED_BUILD_MODEL = False
    im_file = './dog_bike_car_640x640.jpg'

    # Create RKNN object
    rknn = RKNN()

    OUT_DIR = "rknn_models"
    RKNN_MODEL_PATH = './{}/{}_rm_transpose_{}.rknn'.format(
        OUT_DIR, exp+'-'+str(Width)+'-'+str(Height), platform)
    if NEED_BUILD_MODEL:
        DATASET = './dataset.txt'
        rknn.config(mean_values=[[0.485, 0.456, 0.406]], std_values=[
                    [0.229, 0.224, 0.225]], target_platform=platform)
        # Load model
        print('--> Loading model')
        ret = rknn.load_onnx(MODEL_PATH)
        if ret != 0:
            print('load model failed!')
            exit(ret)
        print('done')

        # Build model
        print('--> Building model')
        ret = rknn.build(do_quantization=True, dataset=DATASET)
        if ret != 0:
            print('build model failed.')
            exit(ret)
        print('done')

        # Export rknn model
        if not os.path.exists(OUT_DIR):
            os.mkdir(OUT_DIR)
        print('--> Export RKNN model: {}'.format(RKNN_MODEL_PATH))
        ret = rknn.export_rknn(RKNN_MODEL_PATH)
        if ret != 0:
            print('Export rknn model failed.')
            exit(ret)
        print('done')
    else:
        ret = rknn.load_rknn(RKNN_MODEL_PATH)

    rknn.release()

