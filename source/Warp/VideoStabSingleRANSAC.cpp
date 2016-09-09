#include "Rotation.h"
#include "ConvertCoordinate.h"
#include "Stabilize.h"
#include "ZReproject.h"
#include "Blend/ZBlend.h"
#include "Tool/Timer.h"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/features2d.hpp"
#include <cmath>
#include <iostream>
#include <utility>
#include <fstream>

void mapBilinear(const cv::Mat& src, cv::Mat& dst, const cv::Matx33d& rot);
void mapNearestNeighbor(const cv::Mat& src, cv::Mat& dst, const cv::Matx33d& rot);

static void toValidFileName(const std::string& path, std::string& result)
{
    result = path;
    std::replace_if(result.begin(), result.end(), [](char c)->bool {return !(isdigit(c) || isalpha(c)); }, '_');
}

static void toEquiRect(const PhotoParam& param, const cv::Size& srcSize, const cv::Size& dstSize, std::vector<cv::Point2d>& srcPts, std::vector<cv::Point2d>& dstPts)
{
    Remap t;
    t.initInverse(param, dstSize.width, dstSize.height, srcSize.width, srcSize.height);
    int length = srcPts.size();
    dstPts.resize(length);
    for (int i = 0; i < length; i++)
    {
        t.inverseRemapImage(srcPts[i].x, srcPts[i].y, dstPts[i].x, dstPts[i].y);
    }
}

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

static void drawPoints(cv::Mat& image, const std::vector<cv::Point2d>& points, const cv::Scalar& color)
{
    int size = points.size();
    for (int i = 0; i < size; i++)
        cv::circle(image, cv::Point(points[i].x, points[i].y), 2, color);
}

static void drawPoints(cv::Mat& image, const std::vector<cv::Point2d>& src, const std::vector<cv::Point2d>& dst, 
    const cv::Scalar& colorSrc, const cv::Scalar& colorDst)
{
    int size = src.size();
    for (int i = 0; i < size; i++)
    {
        cv::Point p1(src[i].x, src[i].y);
        cv::Point p2(dst[i].x, dst[i].y);
        cv::circle(image, p1, 2, colorSrc);
        cv::circle(image, p2, 2, colorDst);
        cv::line(image, p1, p2, 255);
    }        
}

static void drawSpherePairsOnEquirect(cv::Mat& image, int height, const std::vector<cv::Point3d>& src, const std::vector<cv::Point3d>& dst)
{
    image.create(height, height * 2, CV_8UC1);
    image.setTo(0);
    double halfWidth = height, halfHeight = height * 0.5;
    int size = src.size();
    for (int i = 0; i < size; i++)
    {
        cv::Point2d p1d, p2d;
        p1d = sphereToEquirect(src[i], halfWidth, halfHeight);
        p2d = sphereToEquirect(dst[i], halfWidth, halfHeight);

        cv::Point p1(p1d.x, p1d.y), p2(p2d.x, p2d.y);
        cv::circle(image, p1, 3, 255);
        cv::circle(image, p2, 3, 255);
        cv::line(image, p1, p2, 255);
    }
}

static void shiftPoints(const std::vector<cv::Point2d>& src, std::vector<cv::Point2d>& dst, cv::Point2d offset)
{
    int size = src.size();
    dst.resize(size);
    for (int i = 0; i < size; i++)
        dst[i] = src[i] + offset;
}

int main()
{
    cv::Ptr<cv::ORB> ptrOrb = cv::ORB::create(250);
    cv::BFMatcher matcher(cv::NORM_L2, true);
    const char* videoPath = "F:\\QQRecord\\452103256\\FileRecv\\mergetest2new.avi";
    cv::VideoCapture cap(videoPath);
    int numFrames = cap.get(CV_CAP_PROP_FRAME_COUNT);
    cv::Size frameSize;
    frameSize.width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
    frameSize.height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);

    std::vector<cv::Vec3d> angles;
    angles.reserve(numFrames);

    int cubeLength = 512;
    cv::Mat map;
    getEquiRectToCubeMap(map, frameSize.height, cubeLength, CubeType3x2);

    std::vector<cv::Rect> squares;
    squares.push_back(cv::Rect(0, 0, cubeLength, cubeLength));
    squares.push_back(cv::Rect(cubeLength, 0, cubeLength, cubeLength));
    squares.push_back(cv::Rect(2 * cubeLength, 0, cubeLength, cubeLength));
    squares.push_back(cv::Rect(0, cubeLength, cubeLength, cubeLength));
    squares.push_back(cv::Rect(cubeLength, cubeLength, cubeLength, cubeLength));
    squares.push_back(cv::Rect(2 * cubeLength, cubeLength, cubeLength, cubeLength));

    std::vector<cv::Point2d> offsets;
    offsets.push_back(cv::Point2d(0, 0));
    offsets.push_back(cv::Point2d(cubeLength, 0));
    offsets.push_back(cv::Point2d(2 * cubeLength, 0));
    offsets.push_back(cv::Point2d(0, cubeLength));
    offsets.push_back(cv::Point2d(cubeLength, cubeLength));
    offsets.push_back(cv::Point2d(2 * cubeLength, cubeLength));

    int numVideos = 6;
    cv::Mat frame, cubeFrame, gray, grayPart;
    std::vector<cv::Mat> grays(numVideos);
    std::vector<cv::Mat> descsPrev(numVideos), descsCurr(numVideos);
    std::vector<std::vector<cv::KeyPoint> > pointsPrev(numVideos), pointsCurr(numVideos);
    std::vector<std::vector<cv::DMatch> > matches(numVideos);
    std::vector<std::vector<cv::Point2d> > points1(numVideos), points2(numVideos);
    std::vector<std::vector<cv::Point2d> > srcEquiRectPts(numVideos), dstEquiRectPts(numVideos);
    std::vector<std::vector<cv::Point3d> > srcSpherePts(numVideos), dstSpherePts(numVideos);
    std::vector<cv::Point3d> src, dst;
    std::vector<unsigned char> mask;

    cap.read(frame);
    reprojectParallel(frame, cubeFrame, map);
    cv::cvtColor(cubeFrame, gray, CV_BGR2GRAY);
    for (int i = 0; i < numVideos; i++)
    {
        ptrOrb->detectAndCompute(gray(squares[i]), cv::Mat(), pointsPrev[i], descsPrev[i]);
    }
    angles.push_back(cv::Vec3d(0, 0, 0));

    int count = 0;

    cv::Mat show, showMatch;

    std::vector<cv::Point2d> p1, p2;

    while (true)
    {
        bool success = cap.read(frame);
        ++count;
        if (count > 1500 || !success)
            break;

        reprojectParallel(frame, cubeFrame, map);
        
        cv::cvtColor(cubeFrame, gray, CV_BGR2GRAY);
        cubeFrame.copyTo(show);
        for (int i = 0; i < numVideos; i++)
        {
            ptrOrb->detectAndCompute(gray(squares[i]), cv::Mat(), pointsCurr[i], descsCurr[i]);
            matcher.match(descsPrev[i], descsCurr[i], matches[i]);
            filterMatches(pointsPrev[i], pointsCurr[i], matches[i]);
            extractMatchPoints(pointsPrev[i], pointsCurr[i], matches[i], points1[i], points2[i]);
            
            cubeToSphere(points1[i], cubeLength, i, srcSpherePts[i]);
            cubeToSphere(points2[i], cubeLength, i, dstSpherePts[i]);

            //drawPoints(show, points1[i], cv::Scalar(255));
            //drawPoints(show, points2[i], cv::Scalar(0, 255));
            //shiftPoints(points1[i], p1, offsets[i]);
            //shiftPoints(points2[i], p2, offsets[i]);
            //drawPoints(show, p1, p2, 255, cv::Scalar(0, 255));
        }

        //cv::imshow("cube frame", show);
        //cv::waitKey(1);

        int pointCount = 0;
        for (int i = 0; i < numVideos; i++)
        {
            pointCount += srcSpherePts[i].size();
        }

        src.clear();
        dst.clear();
        src.resize(pointCount);
        dst.resize(pointCount);
        std::vector<cv::Point3d>::iterator itrSrc = src.begin(), itrDst = dst.begin();
        for (int i = 0; i < numVideos; i++)
        {
            itrSrc = std::copy(srcSpherePts[i].begin(), srcSpherePts[i].end(), itrSrc);
            itrDst = std::copy(dstSpherePts[i].begin(), dstSpherePts[i].end(), itrDst);
        }

        //drawSpherePairsOnEquirect(showMatch, 800, src, dst);
        //cv::imshow("match", showMatch);
        //cv::waitKey(1);

        cv::Matx33d currRot;
        cv::Point3d currTranslation;
        double yaw, pitch, roll;
        //checkMatchedPointsDist(src, dst);
        getRigidTransformRANSAC(src, dst, currRot, currTranslation, mask);
        getRotationRM(currRot, yaw, pitch, roll);
        angles.push_back(cv::Vec3d(yaw, pitch, roll));
        printf("yaw = %f, pitch = %f, roll = %f\n", yaw, pitch, roll);

        for (int i = 0; i < numVideos; i++)
        {
            pointsCurr[i].swap(pointsPrev[i]);
            cv::swap(descsCurr[i], descsPrev[i]);
        }
    }

    std::vector<cv::Vec3d> anglesProc;
    smooth(angles, 30, anglesProc);

    std::vector<cv::Vec3d> anglesAccum, anglesProcAccum;
    accumulate(angles, anglesAccum);
    accumulate(anglesProc, anglesProcAccum);

    //frameSize = cv::Size(800, 400);

    const char* outPath = "stab_2.avi";
    cv::VideoWriter writer(outPath, CV_FOURCC('X', 'V', 'I', 'D'), 48, frameSize);

    cap.release();
    cap.open(videoPath);

    cv::Mat rotateImage;
    int frameCount = 0;
    int maxCount = angles.size();
    std::vector<cv::Mat> srcImages(numVideos);
    std::vector<cv::Mat> dstImages, compImages;
    ztool::Timer timer;
    cv::Vec3d accumOrig(0, 0, 0), accumProc(0, 0, 0);
    while (true)
    {
        printf("currCount = %d\n", frameCount);
        bool success = cap.read(frame);
        if (!success)
            break;

        accumOrig += angles[frameCount];
        accumProc += anglesProc[frameCount];
        printf("accumOrig = (%f, %f, %f), accumProc = (%f, %f, %f)\n",
            accumOrig[0], accumOrig[1], accumOrig[2],
            accumProc[0], accumProc[1], accumProc[2]);
        cv::Vec3d diff = accumProc - accumOrig;
        cv::Matx33d rot;
        setRotationRM(rot, diff[0], diff[1], diff[2]);
        mapBilinear(frame, rotateImage, rot);

        writer.write(rotateImage);

        frameCount++;
        if (frameCount >= maxCount)
            break;
    }

    return 0;
}