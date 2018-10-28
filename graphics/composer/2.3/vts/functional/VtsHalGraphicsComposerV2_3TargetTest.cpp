/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "graphics_composer_hidl_hal_test@2.3"

#include <algorithm>

#include <VtsHalHidlTargetTestBase.h>
#include <android-base/logging.h>
#include <composer-command-buffer/2.3/ComposerCommandBuffer.h>
#include <composer-vts/2.1/GraphicsComposerCallback.h>
#include <composer-vts/2.1/TestCommandReader.h>
#include <composer-vts/2.3/ComposerVts.h>

namespace android {
namespace hardware {
namespace graphics {
namespace composer {
namespace V2_3 {
namespace vts {
namespace {

using common::V1_1::PixelFormat;
using common::V1_1::RenderIntent;
using common::V1_2::ColorMode;
using common::V1_2::Dataspace;

// Test environment for graphics.composer
class GraphicsComposerHidlEnvironment : public ::testing::VtsHalHidlTargetTestEnvBase {
   public:
    // get the test environment singleton
    static GraphicsComposerHidlEnvironment* Instance() {
        static GraphicsComposerHidlEnvironment* instance = new GraphicsComposerHidlEnvironment;
        return instance;
    }

    virtual void registerTestServices() override { registerTestService<IComposer>(); }

   private:
    GraphicsComposerHidlEnvironment() {}

    GTEST_DISALLOW_COPY_AND_ASSIGN_(GraphicsComposerHidlEnvironment);
};

class GraphicsComposerHidlTest : public ::testing::VtsHalHidlTargetTestBase {
   protected:
    void SetUp() override {
        ASSERT_NO_FATAL_FAILURE(
            mComposer = std::make_unique<Composer>(
                GraphicsComposerHidlEnvironment::Instance()->getServiceName<IComposer>()));
        ASSERT_NO_FATAL_FAILURE(mComposerClient = mComposer->createClient());

        mComposerCallback = new V2_1::vts::GraphicsComposerCallback;
        mComposerClient->registerCallback(mComposerCallback);

        // assume the first display is primary and is never removed
        mPrimaryDisplay = waitForFirstDisplay();

        mInvalidDisplayId = GetInvalidDisplayId();

        // explicitly disable vsync
        mComposerClient->setVsyncEnabled(mPrimaryDisplay, false);
        mComposerCallback->setVsyncAllowed(false);

        mWriter = std::make_unique<CommandWriterBase>(1024);
        mReader = std::make_unique<V2_1::vts::TestCommandReader>();
    }

    void TearDown() override {
        if (mComposerCallback != nullptr) {
            EXPECT_EQ(0, mComposerCallback->getInvalidHotplugCount());
            EXPECT_EQ(0, mComposerCallback->getInvalidRefreshCount());
            EXPECT_EQ(0, mComposerCallback->getInvalidVsyncCount());
        }
    }

    // returns an invalid display id (one that has not been registered to a
    // display.  Currently assuming that a device will never have close to
    // std::numeric_limit<uint64_t>::max() displays registered while running tests
    Display GetInvalidDisplayId() {
        std::vector<Display> validDisplays = mComposerCallback->getDisplays();
        uint64_t id = std::numeric_limits<uint64_t>::max();
        while (id > 0) {
            if (std::find(validDisplays.begin(), validDisplays.end(), id) == validDisplays.end()) {
                return id;
            }
            id--;
        }

        return 0;
    }

    void execute() { mComposerClient->execute(mReader.get(), mWriter.get()); }

    // use the slot count usually set by SF
    static constexpr uint32_t kBufferSlotCount = 64;

    std::unique_ptr<Composer> mComposer;
    std::unique_ptr<ComposerClient> mComposerClient;
    sp<V2_1::vts::GraphicsComposerCallback> mComposerCallback;
    // the first display and is assumed never to be removed
    Display mPrimaryDisplay;
    Display mInvalidDisplayId;
    std::unique_ptr<CommandWriterBase> mWriter;
    std::unique_ptr<V2_1::vts::TestCommandReader> mReader;

   private:
    Display waitForFirstDisplay() {
        while (true) {
            std::vector<Display> displays = mComposerCallback->getDisplays();
            if (displays.empty()) {
                usleep(5 * 1000);
                continue;
            }

            return displays[0];
        }
    }
};

/**
 * Test IComposerClient::getDisplayIdentificationData.
 *
 * TODO: Check that ports are unique for multiple displays.
 */
TEST_F(GraphicsComposerHidlTest, GetDisplayIdentificationData) {
    uint8_t port0;
    std::vector<uint8_t> data0;
    if (mComposerClient->getDisplayIdentificationData(mPrimaryDisplay, &port0, &data0)) {
        uint8_t port1;
        std::vector<uint8_t> data1;
        ASSERT_TRUE(mComposerClient->getDisplayIdentificationData(mPrimaryDisplay, &port1, &data1));

        ASSERT_EQ(port0, port1) << "ports are not stable";
        ASSERT_TRUE(data0.size() == data1.size() &&
                    std::equal(data0.begin(), data0.end(), data1.begin()))
            << "data is not stable";
    }
}

/**
 * TestIComposerClient::getReadbackBufferAttributes_2_3
 */
TEST_F(GraphicsComposerHidlTest, GetReadbackBufferAttributes_2_3) {
    Dataspace dataspace;
    PixelFormat pixelFormat;

    ASSERT_NO_FATAL_FAILURE(mComposerClient->getReadbackBufferAttributes_2_3(
        mPrimaryDisplay, &pixelFormat, &dataspace));
}

/**
 * Test IComposerClient::getClientTargetSupport_2_3
 */
TEST_F(GraphicsComposerHidlTest, GetClientTargetSupport_2_3) {
    std::vector<V2_1::Config> configs = mComposerClient->getDisplayConfigs(mPrimaryDisplay);
    for (auto config : configs) {
        int32_t width = mComposerClient->getDisplayAttribute(mPrimaryDisplay, config,
                                                             IComposerClient::Attribute::WIDTH);
        int32_t height = mComposerClient->getDisplayAttribute(mPrimaryDisplay, config,
                                                              IComposerClient::Attribute::HEIGHT);
        ASSERT_LT(0, width);
        ASSERT_LT(0, height);

        mComposerClient->setActiveConfig(mPrimaryDisplay, config);

        ASSERT_TRUE(mComposerClient->getClientTargetSupport_2_3(
            mPrimaryDisplay, width, height, PixelFormat::RGBA_8888, Dataspace::UNKNOWN));
    }
}
/**
 * Test IComposerClient::getClientTargetSupport_2_3
 *
 * Test that IComposerClient::getClientTargetSupport_2_3 returns
 * Error::BAD_DISPLAY when passed in an invalid display handle
 */

TEST_F(GraphicsComposerHidlTest, GetClientTargetSupport_2_3BadDisplay) {
    std::vector<V2_1::Config> configs = mComposerClient->getDisplayConfigs(mPrimaryDisplay);
    for (auto config : configs) {
        int32_t width = mComposerClient->getDisplayAttribute(mPrimaryDisplay, config,
                                                             IComposerClient::Attribute::WIDTH);
        int32_t height = mComposerClient->getDisplayAttribute(mPrimaryDisplay, config,
                                                              IComposerClient::Attribute::HEIGHT);
        ASSERT_LT(0, width);
        ASSERT_LT(0, height);

        mComposerClient->setActiveConfig(mPrimaryDisplay, config);

        Error error = mComposerClient->getRaw()->getClientTargetSupport_2_3(
            mInvalidDisplayId, width, height, PixelFormat::RGBA_8888, Dataspace::UNKNOWN);

        EXPECT_EQ(Error::BAD_DISPLAY, error);
    }
}

/**
 * Test IComposerClient::getRenderIntents_2_3
 */
TEST_F(GraphicsComposerHidlTest, GetRenderIntents_2_3) {
    std::vector<ColorMode> modes = mComposerClient->getColorModes_2_3(mPrimaryDisplay);
    for (auto mode : modes) {
        std::vector<RenderIntent> intents =
            mComposerClient->getRenderIntents_2_3(mPrimaryDisplay, mode);

        bool isHdr;
        switch (mode) {
            case ColorMode::BT2100_PQ:
            case ColorMode::BT2100_HLG:
                isHdr = true;
                break;
            default:
                isHdr = false;
                break;
        }
        RenderIntent requiredIntent =
            isHdr ? RenderIntent::TONE_MAP_COLORIMETRIC : RenderIntent::COLORIMETRIC;

        auto iter = std::find(intents.cbegin(), intents.cend(), requiredIntent);
        EXPECT_NE(intents.cend(), iter);
    }
}

/*
 * Test IComposerClient::getRenderIntents_2_3
 *
 * Test that IComposerClient::getRenderIntents_2_3 returns Error::BAD_DISPLAY when
 * passed an invalid display handle
 */
TEST_F(GraphicsComposerHidlTest, GetRenderIntents_2_3BadDisplay) {
    std::vector<ColorMode> modes = mComposerClient->getColorModes_2_3(mPrimaryDisplay);
    for (auto mode : modes) {
        mComposerClient->getRaw()->getRenderIntents_2_3(
            mInvalidDisplayId, mode,
            [&](const auto& tmpError, const auto&) { EXPECT_EQ(Error::BAD_DISPLAY, tmpError); });
    }
}

/*
 * Test IComposerClient::getRenderIntents_2_3
 *
 * Test that IComposerClient::getRenderIntents_2_3 returns Error::BAD_PARAMETER when
 * pased either an invalid Color mode or an invalid Render Intent
 */
TEST_F(GraphicsComposerHidlTest, GetRenderIntents_2_3BadParameter) {
    mComposerClient->getRaw()->getRenderIntents_2_3(
        mPrimaryDisplay, static_cast<ColorMode>(-1),
        [&](const auto& tmpError, const auto&) { EXPECT_EQ(Error::BAD_PARAMETER, tmpError); });
}

/**
 * IComposerClient::getColorModes_2_3
 */
TEST_F(GraphicsComposerHidlTest, GetColorModes_2_3) {
    std::vector<ColorMode> colorModes = mComposerClient->getColorModes_2_3(mPrimaryDisplay);

    auto native = std::find(colorModes.cbegin(), colorModes.cend(), ColorMode::NATIVE);
    ASSERT_NE(colorModes.cend(), native);
}

/*
 * Test IComposerClient::getColorModes_2_3
 *
 * Test that IComposerClient::getColorModes_2_3 returns Error::BAD_DISPLAY when
 * passed an invalid display handle
 */
TEST_F(GraphicsComposerHidlTest, GetColorMode_2_3BadDisplay) {
    mComposerClient->getRaw()->getColorModes_2_3(
        mInvalidDisplayId,
        [&](const auto& tmpError, const auto&) { ASSERT_EQ(Error::BAD_DISPLAY, tmpError); });
}

/**
 * IComposerClient::setColorMode_2_3
 */
TEST_F(GraphicsComposerHidlTest, SetColorMode_2_3) {
    std::vector<ColorMode> colorModes = mComposerClient->getColorModes_2_3(mPrimaryDisplay);
    for (auto mode : colorModes) {
        std::vector<RenderIntent> intents =
            mComposerClient->getRenderIntents_2_3(mPrimaryDisplay, mode);
        for (auto intent : intents) {
            ASSERT_NO_FATAL_FAILURE(
                mComposerClient->setColorMode_2_3(mPrimaryDisplay, mode, intent));
        }
    }

    ASSERT_NO_FATAL_FAILURE(mComposerClient->setColorMode_2_3(mPrimaryDisplay, ColorMode::NATIVE,
                                                              RenderIntent::COLORIMETRIC));
}

/*
 * Test IComposerClient::setColorMode_2_3
 *
 * Test that IComposerClient::setColorMode_2_3 returns an Error::BAD_DISPLAY
 * when passed an invalid display handle
 */
TEST_F(GraphicsComposerHidlTest, SetColorMode_2_3BadDisplay) {
    Error error = mComposerClient->getRaw()->setColorMode_2_3(mInvalidDisplayId, ColorMode::NATIVE,
                                                              RenderIntent::COLORIMETRIC);

    ASSERT_EQ(Error::BAD_DISPLAY, error);
}

/*
 * Test IComposerClient::setColorMode_2_3
 *
 * Test that IComposerClient::setColorMode_2_3 returns Error::BAD_PARAMETER when
 * passed an invalid Color mode or an invalid render intent
 */
TEST_F(GraphicsComposerHidlTest, SetColorMode_2_3BadParameter) {
    Error colorModeError = mComposerClient->getRaw()->setColorMode_2_3(
        mPrimaryDisplay, static_cast<ColorMode>(-1), RenderIntent::COLORIMETRIC);
    EXPECT_EQ(Error::BAD_PARAMETER, colorModeError);

    Error renderIntentError = mComposerClient->getRaw()->setColorMode_2_3(
        mPrimaryDisplay, ColorMode::NATIVE, static_cast<RenderIntent>(-1));
    EXPECT_EQ(Error::BAD_PARAMETER, renderIntentError);
}

/**
 * Test IComposerClient::Command::SET_LAYER_COLOR_TRANSFORM.
 * TODO Add color to the layer, use matrix to keep only red component,
 * and check.
 */
TEST_F(GraphicsComposerHidlTest, SetLayerColorTransform) {
    Layer layer;
    ASSERT_NO_FATAL_FAILURE(layer =
                                mComposerClient->createLayer(mPrimaryDisplay, kBufferSlotCount));
    mWriter->selectDisplay(mPrimaryDisplay);
    mWriter->selectLayer(layer);

    // clang-format off
    const std::array<float, 16> matrix = {{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    // clang-format on

    mWriter->setLayerColorTransform(matrix.data());
    execute();
}

}  // namespace
}  // namespace vts
}  // namespace V2_3
}  // namespace composer
}  // namespace graphics
}  // namespace hardware
}  // namespace android

int main(int argc, char** argv) {
    using android::hardware::graphics::composer::V2_3::vts::GraphicsComposerHidlEnvironment;
    ::testing::AddGlobalTestEnvironment(GraphicsComposerHidlEnvironment::Instance());
    ::testing::InitGoogleTest(&argc, argv);
    GraphicsComposerHidlEnvironment::Instance()->init(&argc, argv);
    int status = RUN_ALL_TESTS();
    return status;
}