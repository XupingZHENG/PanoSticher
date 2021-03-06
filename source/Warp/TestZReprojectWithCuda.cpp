#include "ZReproject.h"
#include "CudaAccel/CudaInterface.h"
#include "Tool/Timer.h"
#include "opencv2/core.hpp"
#include "opencv2/core/cuda.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

void compare32FAnd64F(const cv::Mat& mat32F, const cv::Mat& mat64F)
{
    CV_Assert(mat32F.size() == mat64F.size() &&
        mat32F.type() == CV_32FC1 && mat64F.type() == CV_64FC1);

    int rows = mat32F.rows, cols = mat64F.cols;
    cv::Mat diff = cv::Mat::zeros(rows, cols, CV_8UC1);
    for (int i = 0; i < rows; i++)
    {
        const float* ptr32F = mat32F.ptr<float>(i);
        const double* ptr64F = mat64F.ptr<double>(i);
        unsigned char* ptrDiff = diff.ptr<unsigned char>(i);
        for (int j = 0; j < cols; j++)
        {
            if (abs(ptr32F[j] - ptr64F[j]) > 0.001)
            {
                //printf("y = %d, x = %d, %f, %f\n", j, i, ptr32F[j], ptr64F[j]);
                ptrDiff[j] = 255;
            }
                
        }
    }
    cv::imshow("diff", diff);
    cv::waitKey(0);
}

void compare32F(const cv::Mat& a, const cv::Mat& b)
{
    CV_Assert(a.size() == b.size() &&
        a.type() == CV_32FC1 && b.type() == CV_32FC1);

    int rows = a.rows, cols = a.cols;
    cv::Mat diff = cv::Mat::zeros(rows, cols, CV_8UC1);
    for (int i = 0; i < rows; i++)
    {
        const float* ptrA = a.ptr<float>(i);
        const float* ptrB = b.ptr<float>(i);
        unsigned char* ptrDiff = diff.ptr<unsigned char>(i);
        for (int j = 0; j < cols; j++)
        {
            if (abs(ptrA[j] - ptrB[j]) > 0.001F)
            {
                //printf("y = %d, x = %d, %f, %f\n", j, i, ptrA[j], ptrB[j]);
                ptrDiff[j] = 255;
            }

        }
    }
    cv::imshow("diff", diff);
    cv::waitKey(0);
}

int main()
{
    int height = 800;
    cv::Size dstSize = cv::Size(height * 2, height);

    //{
    //    cv::Mat ss(2000, 4000, CV_64FC4);
    //    cv::cuda::GpuMat dd(ss);
    //    //dd.upload(ss);
    //    printf("OK\n");

    //    std::vector<std::string> paths;
    //    paths.push_back("F:\\panoimage\\2\\1\\1.jpg");
    //    paths.push_back("F:\\panoimage\\2\\1\\2.jpg");
    //    paths.push_back("F:\\panoimage\\2\\1\\3.jpg");
    //    paths.push_back("F:\\panoimage\\2\\1\\4.jpg");
    //    paths.push_back("F:\\panoimage\\2\\1\\5.jpg");
    //    paths.push_back("F:\\panoimage\\2\\1\\6.jpg");

    //    int numImages = paths.size();
    //    std::vector<cv::Mat> src(numImages);
    //    for (int i = 0; i < numImages; i++)
    //        src[i] = cv::imread(paths[i]);

    //    std::vector<PhotoParam> params;
    //    loadPhotoParamFromXML("F:\\panoimage\\2\\1\\distort.xml", params);

    //    std::vector<cv::Mat> maps, masks;
    //    getReprojectMapsAndMasks(params, src[0].size(), dstSize, maps, masks);

    //    std::vector<cv::cuda::GpuMat> xmaps, ymaps;
    //    cudaGenerateReprojectMaps(params, src[0].size(), dstSize, xmaps, ymaps);

    //    cv::cuda::GpuMat gmat;
    //    gmat.create(cv::Size(2048, 1024), CV_8UC4);

    //    cv::Mat splitMats[2];
    //    cv::Mat fromGpuMats[2];
    //    for (int i = 0; i < numImages; i++)
    //    {
    //        cv::split(maps[i], splitMats);
    //        xmaps[i].download(fromGpuMats[0]);
    //        ymaps[i].download(fromGpuMats[1]);
    //        compare(fromGpuMats[0], splitMats[0]);
    //        compare(fromGpuMats[1], splitMats[1]);
    //    }
    //}
    //printf("finish\n");

    //{
    //    std::vector<PhotoParam> params;
    //    //loadPhotoParamFromXML("F:\\panovideo\\test\\test1\\haiyangguansimple.xml", params);
    //    loadPhotoParamFromXML("F:\\panoimage\\beijing\\temp_camera_param_new.xml", params);
    //    cv::Mat cpuXMap, cpuYMap, cpuMask, dlXMap, dlYMap, dlMask, maskDiff;
    //    cv::cuda::GpuMat gpuXMap, gpuYMap, gpuMask;
    //    int numParams = params.size();
    //    cv::Size srcSize(1920, 1440), dstSize(2048, 1024);
    //    for (int i = 0; i < numParams; i++)
    //    {
    //        getReprojectMap32FAndMask(params[i], srcSize, dstSize, cpuXMap, cpuYMap, cpuMask);
    //        cudaGenerateReprojectMapAndMask(params[i], srcSize, dstSize, gpuXMap, gpuYMap, gpuMask);
    //        gpuXMap.download(dlXMap);
    //        gpuYMap.download(dlYMap);
    //        gpuMask.download(dlMask);
    //        compare32F(cpuXMap, dlXMap);
    //        compare32F(cpuYMap, dlYMap);

    //        cv::imshow("mask diff", cpuMask != dlMask);
    //        cv::waitKey(0);
    //    };
    //}

    {
        ztool::Timer t;
        std::vector<std::string> paths;
        std::vector<PhotoParam> params;

        //paths.push_back("F:\\panoimage\\detuoffice2\\input-01.jpg");
        //paths.push_back("F:\\panoimage\\detuoffice2\\input-00.jpg");
        //paths.push_back("F:\\panoimage\\detuoffice2\\input-03.jpg");
        //paths.push_back("F:\\panoimage\\detuoffice2\\input-02.jpg");
        //loadPhotoParamFromPTS("F:\\panoimage\\detuoffice2\\4port.pts", params);
        //loadPhotoParamFromXML("F:\\panoimage\\detuoffice2\\detu.xml", params);
        //rotateCameras(params, 0, 0, 3.1415926 / 2);

        paths.push_back("F:\\panoimage\\919-4\\snapshot0.bmp");
        paths.push_back("F:\\panoimage\\919-4\\snapshot1.bmp");
        paths.push_back("F:\\panoimage\\919-4\\snapshot2.bmp");
        paths.push_back("F:\\panoimage\\919-4\\snapshot3.bmp");        
        loadPhotoParamFromXML("F:\\panoimage\\919-4\\vrdl4.xml", params);

        //paths.push_back("F:\\panovideo\\ricoh m15\\image0.bmp");
        //loadPhotoParamFromXML("F:\\panovideo\\ricoh m15\\parambestcircle.xml", params);

        //paths.push_back("F:\\panoimage\\beijing\\image0.bmp");
        //paths.push_back("F:\\panoimage\\beijing\\image1.bmp");
        //paths.push_back("F:\\panoimage\\beijing\\image2.bmp");
        //paths.push_back("F:\\panoimage\\beijing\\image3.bmp");
        //paths.push_back("F:\\panoimage\\beijing\\image4.bmp");
        //paths.push_back("F:\\panoimage\\beijing\\image5.bmp");
        //loadPhotoParamFromXML("F:\\panoimage\\beijing\\temp_camera_param_new.xml", params);
        //rotateCameras(params, 0, 3.1415926536 / 2 * 0.65, 0);

        int numImages = paths.size();
        std::vector<cv::Mat> src(numImages);
        cv::Mat temp;
        for (int i = 0; i < numImages; i++)
        {
            temp = cv::imread(paths[i]);
            cv::cvtColor(temp, src[i], CV_BGR2BGRA);
        }

        //numImages = 2;
        //src.push_back(src[0]);

        std::vector<cv::Mat> maps, masks;
        getReprojectMapsAndMasks(params, src[0].size(), dstSize, maps, masks);
        for (int i = 0; i < numImages; i++)
        {
            cv::imshow("mask", masks[i]);
            cv::waitKey(0);
        }
        
        std::vector<cv::Mat> reproj(numImages);
        t.start();
        for (int i = 0; i < 100; i++)
            reprojectParallel(src, reproj, maps);
        t.end();
        printf("cpu reproj time = %f\n", t.elapse());

        std::vector<cv::cuda::GpuMat> xmaps, ymaps;
        t.start();
        for (int i = 0; i < 100; i++)
            cudaGenerateReprojectMaps(params, src[0].size(), dstSize, xmaps, ymaps);
        t.end();
        printf("gen map time = %f\n", t.elapse());

        std::vector<cv::cuda::GpuMat> srcGpu(numImages), reprojGpu(numImages);
        cv::Mat matC4;
        for (int i = 0; i < numImages; i++)
            srcGpu[i].upload(src[i]);

        for (int i = 0; i < numImages; i++)
            reprojGpu[i].create(dstSize, CV_8UC4);

        t.start();
        for (int i = 0; i < 100; i++)
        {
            for (int j = 0; j < numImages; j++)
                cudaReproject(srcGpu[j], reprojGpu[j], dstSize, params[j]);
        }
        t.end();
        printf("reproj no map time %f\n", t.elapse());

        for (int i = 0; i < numImages; i++)
        {
            reprojGpu[i].download(matC4);
            cv::imshow("dst", matC4);
            cv::waitKey(0);
        }

        t.start();
        for (int i = 0; i < 100; i++)
        {
            for (int j = 0; j < numImages; j++)
                cudaReproject(srcGpu[j], reprojGpu[j], xmaps[j], ymaps[j]);
        }
        t.end();
        printf("reproj with map time %f\n", t.elapse());

        for (int i = 0; i < numImages; i++)
        {
            reprojGpu[i].download(matC4);
            cv::imshow("dst", matC4);
            cv::waitKey(0);
        }

        cv::cuda::GpuMat orig;
        cv::cuda::GpuMat rprj;
        cv::Mat rprj16S, rprj8U;
        //rprj.create(dstSize, CV_16SC4);
        //rprj.download(rprj16S);
        for (int i = 0; i < numImages; i++)
        {
            orig.upload(src[i]);
            //rprj.create(dstSize, CV_16SC4);
            cudaReprojectTo16S(orig, rprj, xmaps[i], ymaps[i]);
            rprj.download(rprj16S);
            rprj16S.convertTo(rprj8U, CV_8U);
            cv::imshow("dst", rprj8U);
            cv::waitKey(0);
        }

        cv::Mat splitMats[2];
        cv::Mat fromGpuMats[2];
        for (int i = 0; i < numImages; i++)
        {
            cv::split(maps[i], splitMats);
            xmaps[i].download(fromGpuMats[0]);
            ymaps[i].download(fromGpuMats[1]);
            compare32FAnd64F(fromGpuMats[0], splitMats[0]);
            compare32FAnd64F(fromGpuMats[1], splitMats[1]);
        }
    }

    return 0;
}