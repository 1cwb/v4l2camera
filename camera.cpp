#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
using namespace std;

// VIDIOC_QUERYCAP	查询设备的属性
// VIDIOC_ENUM_FMT 帧格式
// VIDIOC_S_FMT 设置视频帧格式，对应struct v4l2_format
// VIDIOC_G_FMT 获取视频帧格式等
// VIDIOC_REQBUFS 请求/申请若干个帧缓冲区，一般为不少于3个
// VIDIOC_QUERYBUF 查询帧缓冲区在内核空间的长度和偏移量
// VIDIOC_QBUF 将申请到的帧缓冲区全部放入视频采集输出队列
// VIDIOC_STREAMON 开始视频流数据的采集
// VIDIOC_DQBUF 应用程序从视频采集输出队列中取出已含有采集数据的帧缓冲区
// VIDIOC_STREAMOFF 应用程序将该帧缓冲区重新挂入输入队列

// struct v4l2_capability {
// 	__u8	driver[16];    // 驱动模块的名字
// 	__u8	card[32];      // 设备名字
// 	__u8	bus_info[32];  // 总线信息
// 	__u32   version;       // 内核版本
// 	__u32	capabilities;  // 整个物理设备支持的功能
// 	__u32	device_caps;   // 通过这个特定设备访问的功能
// 	__u32	reserved[3];
// };

class Camera
{
public:
    Camera(const std::string& device, uint16_t width = 1280, uint16_t height = 960, uint32_t pixcelFmt = V4L2_PIX_FMT_MJPEG):mfd_(-1)
    {
        openDevice(device);
        getCameraHWFmt();
        setFormat(width, height, pixcelFmt);
        requestBuff();
        remapBuff();
        streamOn();
    }
    ~Camera()
    {
        streamoff();
        closeDevice();
        releaseMmapMemory();
    }
    int32_t fd(){return this->mfd_;}
    void printCameraHWFmtInfo() const
    {
        for(auto& idx : v4fmt_)
        {
            printf("index = %d\n",idx.index);
            printf("flags = %d\n",idx.flags);
            printf("description = %s\n",idx.description);
            printf("pixcelFmt = %c%c%c%c\n",((char*)&idx.pixelformat)[0],((char*)&idx.pixelformat)[1],
                ((char*)&idx.pixelformat)[2],((char*)&idx.pixelformat)[3]);
            //printf("reserved[0] = %u\n",idx.reserved[0]);
        }
    }
    void printCameraCurrentFmtInfo()
    {
        printf("currentPicfmt_.fmt.pix.field = %u\n",currentPicfmt_.fmt.pix.field);
        printf("currentPicfmt_.fmt.pix.width = %u\n",currentPicfmt_.fmt.pix.width);
        printf("currentPicfmt_.fmt.pix.height = %u\n",currentPicfmt_.fmt.pix.height);
        printf("currentPicfmt_.fmt.pix.pixelformat = %c%c%c%c\n",((char*)&currentPicfmt_.fmt.pix.pixelformat)[0],((char*)&currentPicfmt_.fmt.pix.pixelformat)[1],
            ((char*)&currentPicfmt_.fmt.pix.pixelformat)[2],((char*)&currentPicfmt_.fmt.pix.pixelformat)[3]);
    }
    void printCameraResolution()
    {
        for(auto& res : v4rRsolution_)
        {
            if(res.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                printf("support %dx%d\n", res.discrete.width, res.discrete.height);
            }
            else if(res.type == V4L2_FRMSIZE_TYPE_STEPWISE)
            {
                printf("support %dx%d\n", res.discrete.width, res.discrete.height);
            }
        }
    }
    bool getCapture(const string& picName)
    {
        return startCapture(picName);
    }
    bool setPicFormat(uint16_t width = 720, uint16_t height = 640, uint32_t pixcelFmt = V4L2_PIX_FMT_MJPEG)
    {
        releaseMmapMemory();
        if(!streamOff())
        {
            return false;
        }
        v4fmt_.clear();
        v4rRsolution_.clear();
        closeDevice();
        openDevice(device_);
        if(!setFormat(width, height, pixcelFmt))
        {
            return false;
        }
        if(!requestBuff())
        {
            return false;
        }
        if(!remapBuff())
        {
            return false;
        }
        if(!streamOn())
        {
            return false;
        }
        return true;
    }
private:
    bool setFormat(uint16_t width = 720, uint16_t height = 640, uint32_t pixcelFmt = V4L2_PIX_FMT_MJPEG)
    {
        struct v4l2_format picfmt;
        memset(&picfmt, 0, sizeof(v4l2_format));
        picfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        picfmt.fmt.pix.width = width;
        picfmt.fmt.pix.height = height;
        picfmt.fmt.pix.pixelformat = pixcelFmt;//V4L2_PIX_FMT_YUYV;//V4L2_PIX_FMT_MJPEG;
        int ret = ioctl(mfd_, VIDIOC_S_FMT, &picfmt);
        if(ret < 0)
        {
            printf("error! set pixcel fmt fail, errno: %s\n",strerror(errno));
            return false;
        }
        getCurrentFormat();
        return true;
    }
    bool getCurrentFormat()
    {
        memset(&currentPicfmt_, 0, sizeof(v4l2_format));
        currentPicfmt_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int ret = ioctl(mfd_, VIDIOC_G_FMT, &currentPicfmt_);
        if(ret < 0)
        {
            printf("error! set pixcel fmt fail, errno: %s\n",strerror(errno));
            return false;
        }
        return true;
    }
    bool requestBuff()
    {
        v4l2_requestbuffers reqbuff;
        memset(&reqbuff, 0, sizeof(v4l2_requestbuffers));
        reqbuff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        reqbuff.count = 4;
        reqbuff.memory = V4L2_MEMORY_MMAP;
        int ret = ioctl(mfd_, VIDIOC_REQBUFS, &reqbuff);
        if(ret < 0)
        {
            printf("error: request queue buff fail! errno : %s\n",strerror(errno));
            return false;
        }
        return true;
    }
    bool remapBuff()
    {
        int ret = -1;
        v4l2_buffer mapbuff;
        for(int i = 0; i < 4; i++)
        {
            memset(&mapbuff, 0, sizeof(v4l2_buffer));
            mapbuff.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            mapbuff.index = i;
            ret = ioctl(mfd_, VIDIOC_QUERYBUF, &mapbuff);
            if(ret < 0)
            {
                printf("error: ioctl remap error! errno: %s\n",strerror(errno));
                return false;
            }
            //cout << "mapbuff.length is" << mapbuff.length<< "  mapbuff.m.offset is" << mapbuff.m.offset<<endl;
            unsigned char* t = (unsigned char*)mmap(NULL, mapbuff.length, PROT_READ | PROT_READ, MAP_SHARED, mfd_, mapbuff.m.offset);
            if(t == NULL)
            {
                printf("error: mmap return NULL, errno: %s\n",strerror(errno));
            }
            //printf("t is %p\n",t);
            mptr_.insert(std::pair<unsigned char*, v4l2_buffer>(t, mapbuff));
            ret = ioctl(mfd_, VIDIOC_QBUF, &mapbuff);
            if(ret < 0)
            {
                printf("error: ioctl do vidioc_qbuf fail! errno: %s\n",strerror(errno));
                return false;
            }
        }
        return true;
    }
    bool streamOn()
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int ret = ioctl(mfd_, VIDIOC_STREAMON, &type);
        if(ret < 0)
        {
           printf("error: ioctl do VIDIOC_STREAMON fail! errno: %s\n",strerror(errno)); 
           return false;
        }
        return true;
    }
    bool streamOff()
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int ret = ioctl(mfd_, VIDIOC_STREAMOFF, &type);
        if(ret < 0)
        {
           printf("error: ioctl do VIDIOC_STREAMOFF fail! errno: %s\n",strerror(errno)); 
           return false;
        }
        return true;
    }
    bool startCapture(const string& picName)
    {
        v4l2_buffer readbuffer;
        memset(&readbuffer, 0, sizeof(v4l2_buffer));
        readbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        int ret = ioctl(mfd_, VIDIOC_DQBUF, &readbuffer);
        if(ret < 0)
        {
           printf("error: ioctl do VIDIOC_DQBUF fail! errno: %s\n",strerror(errno)); 
           return false;
        }
        ofstream mypic(picName, ios_base::out);
        if(mypic.bad())
        {
            printf("error! open/create %s fail! errno: %s\n",picName.c_str(), strerror(errno));
            return false;
        }
        for(auto& iter : mptr_)
        {
            if(iter.second.index == readbuffer.index)
            {
                mypic.write((char*)iter.first, readbuffer.length);
                mypic.sync_with_stdio();
                mypic.close();
                break;
            }
        }
        ret = ioctl(mfd_, VIDIOC_QBUF, &readbuffer);
        if(ret < 0)
        {
           printf("error: ioctl do VIDIOC_QBUF fail! errno: %s\n",strerror(errno)); 
           return false;
        }
        return true;
    }
    bool openDevice(const std::string& device)
    {
        if(mfd_ >= 0)
        {
            printf("device already opend!\n");
            return true;
        }
        device_.assign(std::move(device));
        mfd_ = open(device_.c_str(), O_RDWR);
        if(mfd_ < 0)
        {
            printf("error! open device %s fail, errno: %s\n",device_.c_str(), strerror(errno));
            return false;
        }
        return true;
    }
    void closeDevice()
    {
        if(mfd_ >= 0)
        {
            close(mfd_);
            mfd_ = -1;
        }
    }
    void releaseMmapMemory()
    {
        for(auto miterator = mptr_.begin(); miterator != mptr_.end();)
        {
            if(miterator->first)
            {
                //printf("miterator->first = %p, miterator->second.length = %u, offset = %u\n",miterator->first, miterator->second.length, miterator->second.m.offset);
                munmap(miterator->first, miterator->second.length);
                mptr_.erase(miterator ++);
            }
        }
    }
    bool getCameraHWFmt()
    {
        int index = 0;
        v4l2_fmtdesc v4fmtTemp;
        while(true)
        {
            memset(&v4fmtTemp, 0, sizeof(v4l2_fmtdesc));
            v4fmtTemp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            v4fmtTemp.index = index++;
            int ret = ioctl(mfd_, VIDIOC_ENUM_FMT, &v4fmtTemp);
            if(ret < 0 && index > 1)
            {
                //printf("Get all index Sucessful\n");
                break;
            }
            else if(ret < 0 && index <= 1)
            {
                printf("error: get camrea fmt fail: errno: %s\n",strerror(errno));
                return false;
            }
            v4fmt_.push_back(v4fmtTemp);
        }
        //get resolution
        struct v4l2_frmsizeenum frmsize;
        for(auto & fmt : v4fmt_)
        {
            memset(&frmsize, 0, sizeof(v4l2_frmsizeenum));
            frmsize.pixel_format = fmt.pixelformat;
            frmsize.index = 0;
            while(ioctl(mfd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0)
            {
                v4rRsolution_.push_back(frmsize);
                frmsize.index ++;
            }
        }
        return true;
    }

private:
    int32_t mfd_;
    std::string device_;
    std::vector<v4l2_fmtdesc>  v4fmt_;
    std::vector<v4l2_frmsizeenum> v4rRsolution_;
    std::map<unsigned char*, v4l2_buffer> mptr_;

    v4l2_format currentPicfmt_;
    
};

int main()
{
    Camera ca("/dev/video4");
    ca.printCameraResolution();
    ca.printCameraHWFmtInfo();
    ca.printCameraCurrentFmtInfo();
    ca.setPicFormat(640, 480);
    for(int i = 0; i < 100; i++)
    {
        char buf[24] = {0};
        snprintf(buf, 24, "%d.jpg",i);
        printf("get pic %d\n",i);
        ca.getCapture(std::move(string(buf)));
    }
    return 0;
}