#include "RicohUtil.h"
#include "PanoramaTaskUtil.h"
#include "Warp/ZReproject.h"
#include "Blend/ZBlend.h"
#include "Blend/VisualManip.h"
#include "CudaAccel/CudaInterface.h"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"

static void smooth(const std::vector<std::vector<double> >& src, int radius, std::vector<std::vector<double> >& dst)
{
    dst.clear();
    if (src.empty())
        return;
    int size = src.size();
    int length = src[0].size();
    dst.resize(size);
    std::vector<double> sum(length);
    for (int i = 0; i < size; i++)
    {
        int beg = std::max(0, i - radius);
        int end = std::min(i + radius, size - 1);
        for (int k = 0; k < length; k++)
            sum[k] = 0;
        for (int j = beg; j <= end; j++)
        {
            for (int k = 0; k < length; k++)
                sum[k] += src[j][k];
        }
        dst[i].resize(length);
        for (int k = 0; k < length; k++)
            dst[i][k] = sum[k] * (1.0 / (end + 1 - beg));
    }
}

struct ShowTiledImages
{
    ShowTiledImages() : hasInit(false) {};
    bool init(int width_, int height_, int numImages_)
    {
        origWidth = width_;
        origHeight = height_;
        numImages = numImages_;

        showWidth = 480;
        showHeight = origHeight * double(showWidth) / double(origWidth) + 0.5;

        int totalWidth = numImages * showWidth;
        if (totalWidth <= screenWidth)
            tileWidth = numImages * showWidth;
        else
            tileWidth = screenWidth;
        tileHeight = ((totalWidth + screenWidth - 1) / screenWidth) * showHeight;

        int horiNumImages = screenWidth / showWidth;
        locations.resize(numImages);
        for (int i = 0; i < numImages; i++)
        {
            int gridx = i % horiNumImages;
            int gridy = i / horiNumImages;
            locations[i] = cv::Rect(gridx * showWidth, gridy * showHeight, showWidth, showHeight);
        }

        hasInit = true;
        return true;
    }
    bool show(const std::string& winName, const std::vector<cv::Mat>& images)
    {
        if (!hasInit)
            return false;

        if (images.size() != numImages)
            return false;

        for (int i = 0; i < numImages; i++)
        {
            if (images[i].rows != origHeight || images[i].cols != origWidth || 
                ((images[i].type() != CV_8UC4) && (images[i].type() != CV_8UC3)))
                return false;
        }

        tileImage.create(tileHeight, tileWidth, images[0].type());
        for (int i = 0; i < numImages; i++)
        {
            cv::Mat curr = tileImage(locations[i]);
            cv::resize(images[i], curr, cv::Size(showWidth, showHeight), 0, 0, CV_INTER_NN);
        }
        cv::imshow(winName, tileImage);

        return true;
    }

    const int screenWidth = 1920;
    int origWidth, origHeight;
    int showWidth, showHeight;
    int numImages;
    int tileWidth, tileHeight;
    cv::Mat tileImage;
    std::vector<cv::Rect> locations;
    bool hasInit;
};

void compensateBGR(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, std::vector<cv::Mat>& results);

void huginCorrect(const std::vector<cv::Mat>& src, const std::vector<PhotoParam>& params,
    std::vector<std::vector<std::vector<unsigned char> > >& luts);

// main 1
int main1()
{
    std::string configFileName = /*"F:\\panovideo\\test\\SP7\\gopro.pvs"*/
        /*"F:\\panovideo\\test\\test7\\changtai.pvs"*/
        /*"F:\\panovideo\\test\\test6\\zhanxiang.xml"*/
        "F:\\panovideo\\test\\test6\\proj.pvs";

    std::vector<std::string> fileNames;
    std::vector<int> offsets;
    loadVideoFileNamesAndOffset(configFileName, fileNames, offsets);

    int numVideos = fileNames.size();
    //int globalOffset = 3000/*1095*/;
    int globalOffset = 150 * 48;
    for (int i = 0; i < numVideos; i++)
        offsets[i] += globalOffset;
    int readSkipCount = 20;

    std::vector<avp::AudioVideoReader3> readers;
    cv::Size srcSize;
    int audioIndex, validFrameCount;
    prepareSrcVideos(fileNames, avp::PixelTypeBGR24, offsets, -1, readers, audioIndex, srcSize, validFrameCount);

    //for (int i = 0; i < numVideos; i++)
    //{
    //    char buf[256];
    //    sprintf(buf, "image%d.bmp", i);
    //    avp::AudioVideoFrame2 f;
    //    readers[i].read(f);
    //    cv::Mat m(f.height, f.width, CV_8UC3, f.data[0], f.steps[0]);
    //    cv::imwrite(buf, m);
    //}
    //return 0;

    ShowTiledImages shower;
    shower.init(srcSize.width, srcSize.height, numVideos);

    cv::Size dstSize(960, 480);
    std::vector<PhotoParam> photoParams;
    loadPhotoParams(configFileName, photoParams);
    std::vector<cv::Mat> masks, maps;
    getReprojectMapsAndMasks(photoParams, srcSize, dstSize, maps, masks);

    std::vector<cv::Mat> images(numVideos), reprojImages(numVideos), 
        adjustImages(numVideos), tintImages(numVideos);
    TilingLinearBlend linearBlender;
    linearBlender.prepare(masks, 75);
    TilingMultibandBlendFast multiBlender;
    multiBlender.prepare(masks, 5, 8);
    cv::Mat bareBlend, adjustLinearBlend, adjustMultiBlend, tintLinearBlend, tintMultiBlend;

    ExposureColorCorrect correct;
    correct.prepare(masks);

    std::vector<std::vector<unsigned char> > luts;
    std::vector<int> corrected;
    std::vector<std::vector<std::vector<unsigned char> > > tintLuts, bgrLuts, huginLuts;

    std::vector<avp::AudioVideoFrame2> frames(numVideos);
    while (true)
    {
        bool ok = true;
        for (int i = 0; i < numVideos; i++)
        {
            ok = readers[i].read(frames[i]);
            if (!ok)
            {
                break;
            }
        }
        if (!ok)
            break;

        for (int i = 0; i < numVideos; i++)
            images[i] = cv::Mat(srcSize, CV_8UC3, frames[i].data[0], frames[i].steps[0]);
        reprojectParallel(images, reprojImages, maps);

        linearBlender.blend(reprojImages, bareBlend);

        //compensate(reprojImages, masks, adjustImages);
        std::vector<double> es, bs, rs;
        std::vector<std::vector<double> > esBGR;
        correct.correctExposureAndWhiteBalance(reprojImages, es, rs, bs);
        correct.correctColorExposure(reprojImages, esBGR);
        ExposureColorCorrect::getExposureLUTs(es, luts);
        ExposureColorCorrect::getExposureAndWhiteBalanceLUTs(es, rs, bs, tintLuts);
        ExposureColorCorrect::getColorExposureLUTs(esBGR, bgrLuts);
        for (int i = 0; i < numVideos; i++)
            transform(reprojImages[i], adjustImages[i], luts[i], masks[i]);
        //for (int i = 0; i < numVideos; i++)
        //    transform(reprojImages[i], adjustImages[i], bgrLuts[i], masks[i]);
        //huginCorrect(images, photoParams, huginLuts);
        //for (int i = 0; i < numVideos; i++)
        //    transform(reprojImages[i], adjustImages[i], huginLuts[i], masks[i]);
        linearBlender.blend(adjustImages, adjustLinearBlend);
        multiBlender.blend(adjustImages, adjustMultiBlend);

        tintAdjust(adjustImages, masks, tintImages);
        //for (int i = 0; i < numVideos; i++)
        //    transform(reprojImages[i], tintImages[i], tintLuts[i]);
        linearBlender.blend(tintImages, tintLinearBlend);
        multiBlender.blend(tintImages, tintMultiBlend);
        
        shower.show("src", images);
        cv::imshow("bare", bareBlend);
        cv::imshow("ajudst linear", adjustLinearBlend);
        cv::imshow("adjust multiband", adjustMultiBlend);
        cv::imshow("tint linear", tintLinearBlend);
        cv::imshow("tint multiband", tintMultiBlend);
        int key = cv::waitKey(0);
        if (key == 'q')
            break;

        for (int i = 0; i < numVideos; i++)
        {
            for (int j = 0; j < readSkipCount; j++)
            {
                ok = readers[i].read(frames[i]);
                if (!ok)
                {
                    break;
                }
            }
        }
        if (!ok)
            break;
    }

    return 0;
}

// main 2
int main2()
{
    std::string configFileName = /*"F:\\panovideo\\test\\SP7\\gopro.pvs"*/
        "F:\\panovideo\\test\\test7\\changtai.pvs"
        /*"F:\\panovideo\\test\\test6\\zhanxiang.xml"*/;

    std::vector<std::string> fileNames;
    std::vector<int> offsets;
    loadVideoFileNamesAndOffset(configFileName, fileNames, offsets);

    int numVideos = fileNames.size();
    int globalOffset = 1095;
    //int globalOffset = 0;
    for (int i = 0; i < numVideos; i++)
        offsets[i] += globalOffset;
    int readSkipCount = 3;
    int interval = readSkipCount + 1;

    std::vector<avp::AudioVideoReader3> readers;
    cv::Size srcSize;
    int audioIndex, validFrameCount;
    prepareSrcVideos(fileNames, avp::PixelTypeBGR24, offsets, -1, readers, audioIndex, srcSize, validFrameCount);

    cv::Size dstSize(960, 480);
    std::vector<PhotoParam> photoParams;
    loadPhotoParams(configFileName, photoParams);
    std::vector<cv::Mat> masks, maps;
    getReprojectMapsAndMasks(photoParams, srcSize, dstSize, maps, masks);

    std::vector<cv::Mat> images(numVideos), reprojImages(numVideos);
    
    ExposureColorCorrect correct;
    correct.prepare(masks);

    std::vector<std::vector<double> > exposures, reds, blues;
    exposures.reserve(validFrameCount / readSkipCount + 20);
    reds.reserve(validFrameCount / readSkipCount + 20);
    blues.reserve(validFrameCount / readSkipCount + 20);

    int numAnalyze = validFrameCount / interval + 1;
    int analyzeCount = 0;
    std::vector<avp::AudioVideoFrame2> frames(numVideos);
    while (true)
    {
        bool ok = true;
        for (int i = 0; i < numVideos; i++)
        {
            ok = readers[i].read(frames[i]);
            if (!ok)
            {
                break;
            }
        }
        if (!ok)
            break;

        //for (int i = 0; i < numVideos; i++)
        //    printf("%6d", frames[i].frameIndex);
        //printf("\n");

        for (int i = 0; i < numVideos; i++)
            images[i] = cv::Mat(srcSize, CV_8UC3, frames[i].data[0], frames[i].steps[0]);
        reprojectParallel(images, reprojImages, maps);

        std::vector<double> es, bs, rs;
        correct.correctExposureAndWhiteBalance(reprojImages, es, rs, bs);
        exposures.push_back(es);
        reds.push_back(rs);
        blues.push_back(bs);

        //printf("e: ");
        //for (int i = 0; i < numVideos; i++)
        //    printf("%8.5f ", es[i]);
        //printf("\nr: ");
        //for (int i = 0; i < numVideos; i++)
        //    printf("%8.5f ", rs[i]);
        //printf("\nb: ");
        //for (int i = 0; i < numVideos; i++)
        //    printf("%8.5f ", bs[i]);
        //printf("\n");

        for (int i = 0; i < numVideos; i++)
        {
            for (int j = 0; j < readSkipCount; j++)
            {
                ok = readers[i].read(frames[i]);
                if (!ok)
                {
                    break;
                }
            }
        }
        if (!ok)
            break;
        analyzeCount++;
        if (analyzeCount % 10 == 0)
            printf("analyze %d\n", analyzeCount);
    }

    printf("analyze finish\n");

    //return 0;

    dstSize.width = 960, dstSize.height = 480;
    avp::AudioVideoWriter3 writer;
    writer.open("video.mp4", "", false, false, "", 0, 0, 0, 0,
        true, "", avp::PixelTypeBGR24, dstSize.width, dstSize.height, readers[0].getVideoFrameRate(), 16000000);
    prepareSrcVideos(fileNames, avp::PixelTypeBGR24, offsets, -1, readers, audioIndex, srcSize, validFrameCount);
    getReprojectMapsAndMasks(photoParams, srcSize, dstSize, maps, masks);

    TilingMultibandBlendFast blender;
    blender.prepare(masks, 8, 4);

    printf("offsets: ");
    for (int i = 0; i < numVideos; i++)
        printf("%3d ", offsets[i]);
    printf("\n");

    std::vector<cv::Mat> adjustImages;
    cv::Mat blendImage;
    unsigned char* data[4] = { 0 };
    int steps[4] = { 0 };
    int count = 0;
    while (true)
    {
        bool ok = true;
        for (int i = 0; i < numVideos; i++)
        {
            ok = readers[i].read(frames[i]);
            if (!ok)
            {
                break;
            }
        }
        if (!ok)
            break;

        for (int i = 0; i < numVideos; i++)
            printf("%6d", frames[i].frameIndex);
        printf("\n");

        for (int i = 0; i < numVideos; i++)
            images[i] = cv::Mat(srcSize, CV_8UC3, frames[i].data[0], frames[i].steps[0]);
        reprojectParallel(images, reprojImages, maps);

        std::vector<double> currExpo(numVideos), currBlue(numVideos), currRed(numVideos);
        int index = count / interval;
        int nextIndex = index + 1;
        if (nextIndex < exposures.size())
        {
            for (int i = 0; i < numVideos; i++)
            {
                double lambda = double(nextIndex * interval - count) / interval;
                double compLambda = 1 - lambda;
                currExpo[i] = exposures[index][i] * lambda + exposures[nextIndex][i] * compLambda;
                currBlue[i] = blues[index][i] * lambda + blues[nextIndex][i] * compLambda;
                currRed[i] = reds[index][i] * lambda + reds[nextIndex][i] * compLambda;
            }
        }
        else
        {
            currExpo = exposures.back();
            currBlue = blues.back();
            currRed = reds.back();
        }

        std::vector<std::vector<std::vector<unsigned char> > > luts;
        ExposureColorCorrect::getExposureAndWhiteBalanceLUTs(currExpo, currRed, currBlue, luts);
        adjustImages.resize(numVideos);
        for (int i = 0; i < numVideos; i++)
            transform(reprojImages[i], adjustImages[i], luts[i], masks[i]);

        blender.blend(adjustImages, blendImage);

        data[0] = blendImage.data;
        steps[0] = blendImage.step;
        avp::AudioVideoFrame2 f(data, steps, avp::PixelTypeBGR24, dstSize.width, dstSize.height, -1LL);
        writer.write(f);
        
        count++;
        //if (count >= 200)
        //    break;

        printf("write %d/%d\n", count, validFrameCount);
    }

    writer.close();

    return 0;
}

#define NEW_VISUAL_CORRECT 1

// main 3
int main3()
{
    std::string configFileName = /*"F:\\panovideo\\test\\SP7\\gopro.pvs"*/
        /*"F:\\panovideo\\test\\test7\\changtai.pvs"*/
        "F:\\panovideo\\test\\test6\\proj.pvs"
        /*"F:\\panovideo\\test\\chengdu\\����VR-��ƴ��è4\\proj.pvs"*/
        /*"F:\\panovideo\\test\\chengdu\\1\\proj.pvs"*/;

    std::vector<std::string> fileNames;
    std::vector<int> offsets;
    loadVideoFileNamesAndOffset(configFileName, fileNames, offsets);

    int numVideos = fileNames.size();
    //int globalOffset = 1095;
    //int globalOffset = 25 * 60 * 3;
    int globalOffset = 2000;
    for (int i = 0; i < numVideos; i++)
        offsets[i] += globalOffset;
    int readSkipCount = 23;
    int interval = readSkipCount + 1;

    std::vector<avp::AudioVideoReader3> readers;
    cv::Size srcSize;
    int audioIndex, validFrameCount;
    prepareSrcVideos(fileNames, avp::PixelTypeBGR24, offsets, -1, readers, audioIndex, srcSize, validFrameCount);
    //for (int i = 0; i < numVideos; i++)
    //{
    //    avp::AudioVideoFrame2 frame;
    //    readers[i].read(frame);
    //    cv::Mat image(frame.height, frame.width, CV_8UC3, frame.data[0], frame.steps[0]);
    //    char buf[128];
    //    sprintf(buf, "image%d.bmp", i);
    //    cv::imwrite(buf, image);
    //}
    //return 0;

    cv::Size dstSize(1280, 640);
    std::vector<PhotoParam> photoParams;
    loadPhotoParams(configFileName, photoParams);
    std::vector<cv::Mat> masks, maps;
    getReprojectMapsAndMasks(photoParams, srcSize, dstSize, maps, masks);

    std::vector<cv::Mat> images(numVideos), reprojImages(numVideos);

#if !NEW_VISUAL_CORRECT
    ExposureColorCorrect correct;
    correct.prepare(masks);
#endif

    std::vector<std::vector<double> > exposures, reds, blues;
    exposures.reserve(validFrameCount / readSkipCount + 20);
    reds.reserve(validFrameCount / readSkipCount + 20);
    blues.reserve(validFrameCount / readSkipCount + 20);

    int numAnalyze = validFrameCount / interval + 1;
    int analyzeCount = 0;
    std::vector<avp::AudioVideoFrame2> frames(numVideos);
    while (true)
    {
        bool ok = true;
        for (int i = 0; i < numVideos; i++)
        {
            ok = readers[i].read(frames[i]);
            if (!ok)
            {
                break;
            }
        }
        if (!ok)
            break;

        //for (int i = 0; i < numVideos; i++)
        //    printf("%6d", frames[i].frameIndex);
        //printf("\n");

        for (int i = 0; i < numVideos; i++)
            images[i] = cv::Mat(srcSize, CV_8UC3, frames[i].data[0], frames[i].steps[0]);
#if NEW_VISUAL_CORRECT
#else
        reprojectParallel(images, reprojImages, maps);
#endif

        std::vector<double> es, bs, rs;
#if NEW_VISUAL_CORRECT
        exposureColorOptimize(images, photoParams, std::vector<int>(), 
            HISTOGRAM, EXPOSURE | WHITE_BALANCE, es, rs, bs);
#else
        correct.correctExposureAndWhiteBalance(reprojImages, es, rs, bs);
#endif
        exposures.push_back(es);
        reds.push_back(rs);
        blues.push_back(bs);

        printf("e: ");
        for (int i = 0; i < numVideos; i++)
            printf("%8.5f ", es[i]);
        printf("\nr: ");
        for (int i = 0; i < numVideos; i++)
            printf("%8.5f ", rs[i]);
        printf("\nb: ");
        for (int i = 0; i < numVideos; i++)
            printf("%8.5f ", bs[i]);
        printf("\n");

        for (int i = 0; i < numVideos; i++)
        {
            for (int j = 0; j < readSkipCount; j++)
            {
                ok = readers[i].read(frames[i]);
                if (!ok)
                {
                    break;
                }
            }
        }
        if (!ok)
            break;
        analyzeCount++;
        if (analyzeCount % 10 == 0)
            printf("analyze %d\n", analyzeCount);
    }

    printf("analyze finish\n");

    std::vector<std::vector<double> > rss, bss;
    smooth(reds, 10, rss);
    smooth(blues, 10, bss);
    reds.swap(rss);
    blues.swap(bss);
    //return 0;

    dstSize.width = 2048, dstSize.height = 1024;
    avp::AudioVideoWriter3 writer;
    writer.open("video_zhanxiang.mp4", "", false, false, "", 0, 0, 0, 0,
        true, "h264_qsv", avp::PixelTypeBGR32, dstSize.width, dstSize.height, readers[0].getVideoFrameRate(), 16000000);
    prepareSrcVideos(fileNames, avp::PixelTypeBGR32, offsets, -1, readers, audioIndex, srcSize, validFrameCount);
    getReprojectMapsAndMasks(photoParams, srcSize, dstSize, maps, masks);

    std::vector<cv::cuda::GpuMat> xmapsGpu, ymapsGpu;
    cudaGenerateReprojectMaps(photoParams, srcSize, dstSize, xmapsGpu, ymapsGpu);

    std::vector<cv::cuda::Stream> streams(numVideos);
    std::vector<cv::cuda::HostMem> srcFrameMems(numVideos);
    std::vector<avp::AudioVideoFrame2> srcFrames(numVideos);
    std::vector<cv::Mat> srcImagesCpu(numVideos);
    std::vector<cv::cuda::GpuMat> srcImagesGpu(numVideos), reprojImagesGpu(numVideos);
    for (int i = 0; i < numVideos; i++)
    {
        srcFrameMems[i].create(srcSize, CV_8UC4);
        unsigned char* data[4] = { srcFrameMems[i].data, 0, 0, 0 };
        int steps[4] = { srcFrameMems[i].step, 0, 0, 0 };
        srcFrames[i] = avp::AudioVideoFrame2(data, steps, avp::PixelTypeBGR32, srcSize.width, srcSize.height, -1LL, -1);
        srcImagesCpu[i] = srcFrameMems[i].createMatHeader();
    }

    CudaTilingMultibandBlendFast blender;
    blender.prepare(masks, 8, 32);

    printf("offsets: ");
    for (int i = 0; i < numVideos; i++)
        printf("%3d ", offsets[i]);
    printf("\n");

    avp::AudioVideoFrame2 dummyAudioFrame;
    cv::cuda::HostMem blendMem(dstSize, CV_8UC4);
    cv::Mat blendImageCpu = blendMem.createMatHeader();
    cv::cuda::GpuMat blendImageGpu;
    unsigned char* data[4] = { 0 };
    int steps[4] = { 0 };
    int count = 0;
    int frameMediaType;
    while (true)
    {
        bool ok = true;
        for (int i = 0; i < numVideos; i++)
        {
            ok = readers[i].readTo(dummyAudioFrame, srcFrames[i], frameMediaType);
            if (!ok)
            {
                break;
            }
        }
        if (!ok)
            break;

        //for (int i = 0; i < numVideos; i++)
        //    printf("%6d", srcFrames[i].frameIndex);
        //printf("\n");

        std::vector<double> currExpo(numVideos), currBlue(numVideos), currRed(numVideos);
        int index = count / interval;
        int nextIndex = index + 1;
        if (nextIndex < exposures.size())
        {
            for (int i = 0; i < numVideos; i++)
            {
                double lambda = double(nextIndex * interval - count) / interval;
                double compLambda = 1 - lambda;
                currExpo[i] = exposures[index][i] * lambda + exposures[nextIndex][i] * compLambda;
                currBlue[i] = blues[index][i] * lambda + blues[nextIndex][i] * compLambda;
                currRed[i] = reds[index][i] * lambda + reds[nextIndex][i] * compLambda;
            }
        }
        else
        {
            currExpo = exposures.back();
            currBlue = blues.back();
            currRed = reds.back();
        }

        std::vector<std::vector<std::vector<unsigned char> > > luts;
#if NEW_VISUAL_CORRECT
        //getExposureColorOptimizeBezierClampedLUTs(currExpo, currRed, currBlue, luts);
        ExposureColorCorrect::getExposureAndWhiteBalanceLUTs(currExpo, currRed, currBlue, luts);
#else
        ExposureColorCorrect::getExposureAndWhiteBalanceLUTs(currExpo, currRed, currBlue, luts);
#endif
        
        for (int i = 0; i < numVideos; i++)
            srcImagesGpu[i].upload(srcImagesCpu[i], streams[i]);
        for (int i = 0; i < numVideos; i++)
            cudaTransform(srcImagesGpu[i], srcImagesGpu[i], luts[i], streams[i]);
        for (int i = 0; i < numVideos; i++)
            cudaReprojectTo16S(srcImagesGpu[i], reprojImagesGpu[i], xmapsGpu[i], ymapsGpu[i], streams[i]);
        for (int i = 0; i < numVideos; i++)
            streams[i].waitForCompletion();

        blender.blend(reprojImagesGpu, blendImageGpu);
        blendImageGpu.download(blendImageCpu);

        data[0] = blendImageCpu.data;
        steps[0] = blendImageCpu.step;
        avp::AudioVideoFrame2 f(data, steps, avp::PixelTypeBGR32, dstSize.width, dstSize.height, -1LL);
        writer.write(f);

        count++;
        //if (count >= 200)
        //    break;

        printf("write %d/%d\n", count, validFrameCount);
    }

    writer.close();

    return 0;
}

// main 4
int main4()
{
    std::string configFileName = /*"F:\\panovideo\\test\\SP7\\gopro.pvs"*/
        "F:\\panovideo\\test\\test7\\changtai2.pvs"
        /*"F:\\panovideo\\test\\test6\\proj.pvs"*/
        /*"F:\\panovideo\\test\\chengdu\\����VR-��ƴ��è4\\proj.pvs"*/
        /*"F:\\panovideo\\test\\chengdu\\1\\proj.pvs"*/;

    std::vector<std::string> fileNames;
    std::vector<int> offsets;
    loadVideoFileNamesAndOffset(configFileName, fileNames, offsets);

    int numVideos = fileNames.size();
    //int globalOffset = 1095;
    //int globalOffset = 25 * 60 * 3;
    int globalOffset = 2000;
    for (int i = 0; i < numVideos; i++)
        offsets[i] += globalOffset;
    int readSkipCount = 23;
    int interval = readSkipCount + 1;

    std::vector<avp::AudioVideoReader3> readers;
    cv::Size srcSize;
    int audioIndex, validFrameCount;
    prepareSrcVideos(fileNames, avp::PixelTypeBGR24, offsets, -1, readers, audioIndex, srcSize, validFrameCount);
    //for (int i = 0; i < numVideos; i++)
    //{
    //    avp::AudioVideoFrame2 frame;
    //    readers[i].read(frame);
    //    cv::Mat image(frame.height, frame.width, CV_8UC3, frame.data[0], frame.steps[0]);
    //    char buf[128];
    //    sprintf(buf, "image%d.bmp", i);
    //    cv::imwrite(buf, image);
    //}
    //return 0;

    cv::Size dstSize(1280, 640);
    std::vector<PhotoParam> photoParams;
    loadPhotoParams(configFileName, photoParams);
    std::vector<cv::Mat> masks, maps;
    getReprojectMapsAndMasks(photoParams, srcSize, dstSize, maps, masks);

    std::vector<cv::Mat> images(numVideos), correctImages(numVideos), reprojImages(numVideos);
    TilingMultibandBlendFastParallel blender;
    blender.prepare(masks, 6, 32);
    cv::Mat blendImage;

#if !NEW_VISUAL_CORRECT
    ExposureColorCorrect correct;
    correct.prepare(masks);
#endif

    std::vector<std::vector<double> > exposures, reds, blues;
    std::vector<std::vector<std::vector<unsigned char> > > luts;
    exposures.reserve(validFrameCount / readSkipCount + 20);
    reds.reserve(validFrameCount / readSkipCount + 20);
    blues.reserve(validFrameCount / readSkipCount + 20);

    int saveCount = 0;

    int numAnalyze = validFrameCount / interval + 1;
    int analyzeCount = 0;
    std::vector<avp::AudioVideoFrame2> frames(numVideos);
    while (true)
    {
        bool ok = true;
        for (int i = 0; i < numVideos; i++)
        {
            ok = readers[i].read(frames[i]);
            if (!ok)
            {
                break;
            }
        }
        if (!ok)
            break;

        //for (int i = 0; i < numVideos; i++)
        //    printf("%6d", frames[i].frameIndex);
        //printf("\n");

        for (int i = 0; i < numVideos; i++)
            images[i] = cv::Mat(srcSize, CV_8UC3, frames[i].data[0], frames[i].steps[0]);
#if NEW_VISUAL_CORRECT
#else
        reprojectParallel(images, reprojImages, maps);
#endif

        std::vector<double> es, bs, rs;
#if NEW_VISUAL_CORRECT
        exposureColorOptimize(images, photoParams, std::vector<int>(),
            HISTOGRAM, EXPOSURE | WHITE_BALANCE, es, rs, bs);
#else
        correct.correctExposureAndWhiteBalance(reprojImages, es, rs, bs);
#endif
        exposures.push_back(es);
        reds.push_back(rs);
        blues.push_back(bs);

        //getExposureColorOptimizeBezierClampedLUTs(es, rs, bs, luts);
        ExposureColorCorrect::getExposureAndWhiteBalanceLUTs(es, rs, bs, luts);
        for (int i = 0; i < numVideos; i++)
        {
            transform(images[i], correctImages[i], luts[i]);
            reprojectParallelTo16S(correctImages[i], reprojImages[i], maps[i]);
        }
        blender.blend(reprojImages, blendImage);

        printf("e: ");
        for (int i = 0; i < numVideos; i++)
            printf("%8.5f ", es[i]);
        printf("\nr: ");
        for (int i = 0; i < numVideos; i++)
            printf("%8.5f ", rs[i]);
        printf("\nb: ");
        for (int i = 0; i < numVideos; i++)
            printf("%8.5f ", bs[i]);
        printf("\n");

        for (int i = 0; i < numVideos; i++)
        {
            for (int j = 0; j < readSkipCount; j++)
            {
                ok = readers[i].read(frames[i]);
                if (!ok)
                {
                    break;
                }
            }
        }
        if (!ok)
            break;
        analyzeCount++;
        if (analyzeCount % 10 == 0)
            printf("analyze %d\n", analyzeCount);

        cv::imshow("blend image", blendImage);
        int key = cv::waitKey(0);
        if (key == 'q')
            break;
        if (key == 's')
        {
            char buf[128];
            for (int i = 0; i < numVideos; i++)
            {
                sprintf(buf, "image%d.bmp", saveCount++);
                cv::imwrite(buf, images[i]);
            }
        }
    }

    printf("analyze finish\n");
    
    return 0;
}