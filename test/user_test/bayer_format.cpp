#include <iostream>

#include <libcamera/transform.h>
#include <libcamera/internal/bayer_format.h>

#include "test.h"

class BayerFormatTest : public Test 
{
protected:
    int run() override
    {
        std::cout << "Start running test: " << self() << "!" << std::endl;

        libcamera::BayerFormat bayerFmt;
        if (bayerFmt.isValid())
        {
            std::cerr << "Error: An empty bayer format has to be invalid" << std::endl;
            return TestFail;
        }

        bayerFmt = libcamera::BayerFormat(libcamera::BayerFormat::Order::BGGR, 8, \
                                            libcamera::BayerFormat::Packing::None);
        if (bayerFmt.isValid() == false)
        {
            std::cerr << "Error: An correct bayer format has to be valid" << std::endl;
            return TestFail;
        }

        // Two bayer formats created with the same order and bit depth
        // have to be equal
        libcamera::BayerFormat expectedBayerFmt(libcamera::BayerFormat::Order::BGGR, 8, \
                                                libcamera::BayerFormat::Packing::None);
        if (bayerFmt != expectedBayerFmt)
        {
            std::cerr << "Error: Comparison of identical formats failed" << std::endl;
            return TestFail;
        }

        // Two bayer formats created with the same order but different bit depth
        // are not equal
        expectedBayerFmt = libcamera::BayerFormat(libcamera::BayerFormat::Order::BGGR, 12, \
                                                    libcamera::BayerFormat::Packing::None);
        if (bayerFmt == expectedBayerFmt)
        {
            std::cerr << "Error: Comparison of different formats failed" << std::endl;
            return TestFail;
        }

        libcamera::V4L2PixelFormat expectedV4l2PixelFmt(V4L2_PIX_FMT_SBGGR8);
        bayerFmt = libcamera::BayerFormat::fromV4L2PixelFormat(expectedV4l2PixelFmt);
        libcamera::V4L2PixelFormat v4l2PixelFmt = bayerFmt.toV4L2PixelFormat();
        if (v4l2PixelFmt != expectedV4l2PixelFmt)
        {
            std::cerr << "Error: Expected: " << expectedV4l2PixelFmt \
                        << ", got: " << v4l2PixelFmt << std::endl;
            return TestFail;
        }

        // Confirm that an unknown V4L2PixelFormat that is not found in the
        // conversion table
        libcamera::V4L2PixelFormat unknownV4l2PixelFmt(V4L2_PIX_FMT_RGB444);
        bayerFmt = libcamera::BayerFormat::fromV4L2PixelFormat(unknownV4l2PixelFmt);
        if (bayerFmt.isValid())
        {
            std::cerr << "Error: Expected an empty bayer format, got: " << bayerFmt.toString() << std::endl;
            return TestFail;
        }

        // Perform a horizontal flip and make sure that the order is adjusted
        bayerFmt  = libcamera::BayerFormat(libcamera::BayerFormat::Order::BGGR, 8, \
                                            libcamera::BayerFormat::Packing::None);
        expectedBayerFmt  = libcamera::BayerFormat(libcamera::BayerFormat::Order::GBRG, 8, \
                                            libcamera::BayerFormat::Packing::None);
        if (bayerFmt.transform(libcamera::Transform::HFlip) != expectedBayerFmt)
        {
            std::cerr << "Error: " << std::endl;
            return TestFail;
        }

        std::cout << "Finish running test: " << self() << "!" << std::endl;

        return TestPass;
    }
};

TEST_REGISTER(BayerFormatTest);