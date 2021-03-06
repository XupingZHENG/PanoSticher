#include "PanoramaTask.h"
#include "Timer.h"
#include <fstream>
#include <QtWidgets/QApplication>

static void parseVideoPathsAndOffsets(const std::string& infoFileName, std::vector<std::string>& videoPath, std::vector<int>& offset)
{
    videoPath.clear();
    offset.clear();

    std::ifstream fstrm(infoFileName);
    std::string line;
    while (!fstrm.eof())
    {
        std::getline(fstrm, line);
        if (line.empty())
            continue;

        std::string::size_type pos = line.find(',');
        if (pos == std::string::npos)
            continue;

        videoPath.push_back(line.substr(0, pos));
        offset.push_back(atoi(line.substr(pos + 1).c_str()));
    }
}

static void displayProgress(double p, void*)
{
    printf("progress %f%\n", p * 100);
}

static void cancelTask(PanoramaLocalDiskTask* task)
{
    std::this_thread::sleep_for(std::chrono::seconds(30));
    if (task)
        task->cancel();
}

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    const char* keys =
        "{a | camera_param_file |  | camera param file path}"
        "{b | video_path_offset_file |  | video path and offset file path}"
        "{c | num_frames_skip | 100 | number of frames to skip}"
        "{d | pano_width | 2048 | pano picture width}"
        "{e | pano_height | 1024 | pano picture height}"
        "{f | pano_video_name | panocpuh264qsv4k.mp4 | xml param file path}"
        "{g | pano_video_num_frames | 1000 | number of frames to write}"
        "{h | use_cuda | false | use gpu to accelerate computation}";

    cv::CommandLineParser parser(argc, argv, keys);

    cv::Size srcSize, dstSize;
    std::vector<std::string> srcVideoNames;
    std::vector<int> offset;
    int numSkip = 1500;
    std::string cameraParamFile, videoPathAndOffsetFile;
    std::string panoVideoName;

    cameraParamFile = parser.get<std::string>("camera_param_file");
    if (cameraParamFile.empty())
    {
        printf("Could not find camera_param_file\n");
        return 0;
    }

    dstSize.width = parser.get<int>("pano_width");
    dstSize.height = parser.get<int>("pano_height");

    videoPathAndOffsetFile = parser.get<std::string>("video_path_offset_file");
    if (videoPathAndOffsetFile.empty())
    {
        printf("Could not find video_path_offset_file\n");
        return 0;
    }
    parseVideoPathsAndOffsets(videoPathAndOffsetFile, srcVideoNames, offset);
    if (srcVideoNames.empty() || offset.empty())
    {
        printf("Could not parse video path and offset\n");
        return 0;
    }

    numSkip = parser.get<int>("num_frames_skip");
    if (numSkip < 0)
        numSkip = 0;
    for (int i = 0; i < offset.size(); i++)
        offset[i] += numSkip;

    panoVideoName = parser.get<std::string>("pano_video_name");

    /*std::unique_ptr<PanoramaLocalDiskTask> task;
    if (parser.get<bool>("use_cuda"))
    task.reset(new QtCudaPanoramaLocalDiskTask);
    else
    task.reset(new QtCPUPanoramaLocalDiskTask);*/

    /*bool ok = task->init(srcVideoNames, offset, cameraParamFile, panoVideoName,
    dstSize.width, dstSize.height, 48000000, displayProgress, 0);*/

    QtCudaPanoramaLocalDiskTask task;

    bool ok = task.init(srcVideoNames, offset, cameraParamFile, panoVideoName,
        dstSize.width, dstSize.height, 48000000);
    if (!ok)
    {
        printf("Could not init panorama local disk task\n");
        return 0;
    }

    //std::thread t(cancelTask, task.get());
    ztool::Timer timer;
    task.run(0);
    timer.end();
    printf("%f\n", timer.elapse());

    //t.join();

    a.exec();
    return 0;
}