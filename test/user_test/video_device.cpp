#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define STRERROR() std::strerror(errno)
#define LOG_ERR() std::cerr << "Error: " << STRERROR() << std::endl;

#define VERIFYOREND(cond, ret)  \
    do                          \
    {                           \
        if (!(cond))            \
            return (ret);       \
    } while (0)

class DeviceFd
{
public:
    DeviceFd(const DeviceFd&) = delete;
    void operator=(const DeviceFd&) = delete;

    DeviceFd()
        : fd_(-1)
    {}

    DeviceFd(DeviceFd&& otherFd)
    {
        fd_ = otherFd.fd_;
        otherFd.fd_ = -1;
    }

    void operator=(DeviceFd&& otherFd)
    {
        if (this != &otherFd)
        {
            fd_ = otherFd.fd_;
            otherFd.fd_ = -1;
        }
    }

    ~DeviceFd()
    {
        Close();   
    }

    int Open(const std::string& dev, mode_t mode)
    {
        fd_ = ::open(dev.c_str(), mode);
        if (fd_ < 0)
        {
            LOG_ERR();
            return -1;
        }

        return 0;
    }

    void Close()
    {
        if (fd_ >= 0)
            ::close(fd_);

        fd_ = -1;
    }

    int Get()
    {
        return fd_;
    }

private:
    int fd_;
};

class V4l2DeviceFormat
{

};

class V4l2BufferCache
{
public:
    class Plane
    {
    public:
        Plane(uint8_t *o = nullptr, unsigned int l = 0)
            : offset(o), length(l)
        {}    

        ~Plane() = default;
    
        uint8_t *offset;
        unsigned int length;
    };

    V4l2BufferCache(unsigned int i = 0, unsigned int l = 0)
        : index(i), length(l)
    {
        planes.clear();
    }

    ~V4l2BufferCache() = default;

    unsigned int index;
    unsigned int length;
    std::vector<Plane> planes;
};

class V4l2Device 
{
public:
    V4l2Device()
        : is_buffer_requested_(false)
    {}

    ~V4l2Device()
    {
        Close();
    }

    int Open(std::string& video_dev)
    {
        int ret = 0;

        ret = fd_.Open(video_dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        VERIFYOREND(ret >= 0, -1);

        ret = ::ioctl(fd_.Get(), VIDIOC_QUERYCAP, &caps_);
        VERIFYOREND(ret >= 0, -1);

        std::cout << "Driver: " << Driver() << std::endl;
        std::cout << "Card: " << Card() << std::endl;
        std::cout << "Bus Info: " << BusInfo() << std::endl;

        if (IsVideoCapture())
        {
            buf_type_ = IsMultiplanar() ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                                    : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        }
        else if (IsVideoOutput())
        {
            buf_type_ = IsMultiplanar() ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
                                    : V4L2_BUF_TYPE_VIDEO_OUTPUT;
        }
        else if (IsMetaCapture())
        {
            buf_type_ = V4L2_BUF_TYPE_META_CAPTURE;
        }
        else if (IsMetaOutput())
        {
            buf_type_ = V4L2_BUF_TYPE_META_OUTPUT;
        }
        else
        {
            buf_type_ = 0;
            std::cerr << "Error: Failed to determine the type of device" << std::endl;
            ret = -EINVAL;
            return ret;
        }

        return ret;
    }

    void Close()
    {
        ReleaseBuffers();
        fd_.Close();
    }

    unsigned int Caps() const
    {
        return caps_.capabilities;
    }

    std::string Driver()
    {
        return std::string(reinterpret_cast<const char *>(caps_.driver));
    }

    std::string Card()
    {
        return std::string(reinterpret_cast<const char *>(caps_.card));
    }
    
    std::string BusInfo()
    {
        return std::string(reinterpret_cast<const char *>(caps_.bus_info));
    }

    bool IsMultiplanar() const
    {
        return Caps() & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | 
                            V4L2_CAP_VIDEO_OUTPUT_MPLANE |
                            V4L2_CAP_VIDEO_M2M_MPLANE);
    }

	bool IsCapture() const
	{
		return Caps() & (V4L2_CAP_VIDEO_CAPTURE |
                            V4L2_CAP_VIDEO_CAPTURE_MPLANE |
                            V4L2_CAP_META_CAPTURE);
	}

	bool IsOutput() const
	{
		return Caps() & (V4L2_CAP_VIDEO_OUTPUT |
                            V4L2_CAP_VIDEO_OUTPUT_MPLANE |
                            V4L2_CAP_META_OUTPUT);
	}

	bool IsVideo() const
	{
		return Caps() & (V4L2_CAP_VIDEO_CAPTURE |
                            V4L2_CAP_VIDEO_CAPTURE_MPLANE |
                            V4L2_CAP_VIDEO_OUTPUT |
                            V4L2_CAP_VIDEO_OUTPUT_MPLANE);
	}

	bool IsM2M() const
	{
		return Caps() & (V4L2_CAP_VIDEO_M2M |
                            V4L2_CAP_VIDEO_M2M_MPLANE);
	}

	bool IsMeta() const
	{
		return Caps() & (V4L2_CAP_META_CAPTURE |
                            V4L2_CAP_META_OUTPUT);
	}

	bool IsVideoCapture() const
	{
		return IsVideo() && IsCapture();
	}

	bool IsVideoOutput() const
	{
		return IsVideo() && IsOutput();
	}

	bool IsMetaCapture() const
	{
		return IsMeta() && IsCapture();
	}
    
	bool IsMetaOutput() const
	{
		return IsMeta() && IsOutput();
	}

    int CreateBuffers(unsigned int count, enum v4l2_memory memory_type)
    {
        int ret = 0;

        memory_type_ = memory_type;

        ret = RequestBuffers_(count, memory_type);
        VERIFYOREND(ret == 0, -1);

        for (unsigned int i = 0; i < count; i++)
        {
            ret = QueryBuffer_(i);
            VERIFYOREND(ret == 0, -1);
        }

        return ret;
    }

    // Release all the requested buffer (and planes as well)
    int ReleaseBuffers()
    {
        int ret = 0;
        
        ret = FreeCaches_();
        VERIFYOREND(ret == 0, -1);

        // Make sure that the buffers already requested before release them
        if (is_buffer_requested_ == true)
        {
            // To free the requested buffer, the count field must be 0
            ret = RequestBuffers_(0, memory_type_);
            VERIFYOREND(ret == 0, -1);

            is_buffer_requested_ = false;
        }

        return ret;
    }

    int SetFormat(V4l2DeviceFormat *format)
    {
        int ret = 0;

        switch (buf_type_)
        {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        case V4L2_BUF_TYPE_VIDEO_OUTPUT:
            ret = TrySetFormatSingleplane_(format, true);
            break;
        case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
        case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
            ret = TrySetFormatMultiplane_(format, true);
            break;
        case V4L2_BUF_TYPE_META_CAPTURE:
        case V4L2_BUF_TYPE_META_OUTPUT:
            ret = TrySetFormatMeta_(format, true);
            break;
        default:
            ret = -EINVAL;
            break;
        }

        return ret;
    }

    int GetFormat(V4l2DeviceFormat *format)
    {
        int ret = 0;

        return ret;
    }

    int EnqueueingBuffers(const V4l2BufferCache& buf_cache)
    {
        int ret = 0;
        struct v4l2_buffer buf_info;

        buf_info.index = buf_cache.index;
        buf_info.type = buf_type_;
        buf_info.memory = memory_type_;

        ret = ::ioctl(fd_.Get(), VIDIOC_QBUF, &buf_info);
        if (ret != 0)
        {
            LOG_ERR();
            return ret;
        }
        
        return ret;
    }

    int DequeueingBuffers()
    {
        int ret = 0;

        return ret;
    }

private:
    DeviceFd fd_;
    struct v4l2_capability caps_;
    unsigned int buf_type_;
    enum v4l2_memory memory_type_;
    std::vector<V4l2BufferCache> caches_;
    bool is_buffer_requested_;

    int RequestBuffers_(unsigned int count, enum v4l2_memory memory_type)
    {
        int ret = 0;
        struct v4l2_requestbuffers req_bufs;

        req_bufs.count = count;
        req_bufs.type = buf_type_;
        req_bufs.memory = memory_type;

        ret = ::ioctl(fd_.Get(), VIDIOC_REQBUFS, &req_bufs);
        if (ret < 0)
        {
            LOG_ERR();
            return ret;
        }

        is_buffer_requested_ = true;

        if (req_bufs.count < count)
        {
            std::cerr << "Not enough buffers provided, got"
                    << req_bufs.count << std::endl;
            ret = -ENOMEM;
        }

        return ret;
    }

    int QueryBuffer_(unsigned int index)
    {
        struct v4l2_buffer buf_info;
        struct v4l2_plane planes_info[VIDEO_MAX_PLANES];
        int ret = 0;

        buf_info.index = index;
        buf_info.type = buf_type_;

        if (IsMultiplanar())
        {
            buf_info.length = sizeof(planes_info) / sizeof(struct v4l2_plane);
            buf_info.m.planes = planes_info;
        }

        ret = ::ioctl(fd_.Get(), VIDIOC_QUERYBUF, &buf_info);
        if (ret < 0)
        {
            LOG_ERR();
            return ret;
        }

        unsigned int num_of_planes = IsMultiplanar() ? buf_info.length : 1;
        if (num_of_planes == 0 || num_of_planes > VIDEO_MAX_PLANES)
        {
            std::cerr << "Error: Invalid number of planes" << std::endl;
            ret = -EINVAL;
            return ret;
        }

        V4l2BufferCache buf_cache(index, num_of_planes);

        for (unsigned int i = 0; i < num_of_planes; i++)
        {
            unsigned int kernelsp_offset = IsMultiplanar() ? planes_info[i].m.mem_offset :
                                                    buf_info.m.offset;
            unsigned int length = IsMultiplanar() ? planes_info[i].length : buf_info.length;

            void *usersp_offset = ::mmap(0, length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd_.Get(), kernelsp_offset);
            
            if (usersp_offset == MAP_FAILED)
            {
                std::cerr << "Error: Failed to mmap the buffer" << std::endl;
                ret = -EINVAL;
                return ret;
            }

            buf_cache.planes.emplace_back(
                V4l2BufferCache::Plane(reinterpret_cast<uint8_t *>(usersp_offset), length));
        }

        caches_.emplace_back(buf_cache);
        return ret;
    }

    int FreeCaches_()
    {
        int ret = 0;

        while (!caches_.empty())
        {
            V4l2BufferCache& buf_cache = caches_.back();
            ret = FreePlanes_(buf_cache);
            if (ret != 0)
            {
                ret = -EINVAL;
                return ret;
            }

            caches_.pop_back();
        }

        return ret;
    }

    // Unmapping all the planes of a v4l2_buffer (buf_cache)
    int FreePlanes_(V4l2BufferCache& buf_cache)
    {
        int ret = 0;

        while(!buf_cache.planes.empty())
        {
            V4l2BufferCache::Plane plane = buf_cache.planes.back();
            ret = ::munmap(plane.offset, plane.length);
            if (ret != 0)
            {
                LOG_ERR();
                return ret;
            }

            buf_cache.planes.pop_back();
        }

        return ret;
    }


    int TrySetFormatSingleplane_(V4l2DeviceFormat *format, bool is_try)
    {
        int ret = 0;

        return ret;
    }

    int TrySetFormatMultiplane_(V4l2DeviceFormat *format, bool is_try)
    {
        int ret = 0;

        return ret;
    }

    int TrySetFormatMeta_(V4l2DeviceFormat *format, bool is_try)
    {
        int ret = 0;

        return ret;
    }
};

int main(int argc, char *argv[])
{
    int ret = 0;

    if (argc < 2)
    {
        std::cout << "Help: missing video device" << std::endl;
        return ret;
    }

    V4l2Device v4l2_dev;
    std::string video_dev = argv[1];

    ret = v4l2_dev.Open(video_dev);
    if (ret != 0)
    {
        std::cerr << "Error: Failed to open video node: " 
                    << video_dev << std::endl;
        v4l2_dev.Close();
        return -1;
    }

    ret = v4l2_dev.CreateBuffers(5, V4L2_MEMORY_MMAP);
    if (ret != 0)
        std::cerr << "Error: Failed to create 5 buffers" << std::endl;

    // Do something here

    ret = v4l2_dev.ReleaseBuffers();
    if (ret != 0)
        std::cerr << "Error: Failed to release 5 buffers" << std::endl;

    return 0;
}