/*
 * Copyright (C) 2019 The LineageOS Project
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

#define LOG_TAG "FingerprintInscreenService"

#include "FingerprintInscreen.h"
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <hidl/HidlTransportSupport.h>
#include <fstream>

#define FINGERPRINT_ACQUIRED_VENDOR 6
#define FINGERPRINT_ERROR_VENDOR 8

#define OP_ENABLE_FP_LONGPRESS 3
#define OP_DISABLE_FP_LONGPRESS 4
#define OP_RESUME_FP_ENROLL 8
#define OP_FINISH_FP_ENROLL 10

#define OP_DISPLAY_AOD_MODE 8
#define OP_DISPLAY_NOTIFY_PRESS 9
#define OP_DISPLAY_SET_DIM 10

#define HBM_ENABLE_PATH "/sys/class/drm/card0-DSI-1/op_friginer_print_hbm"
#define HBM_DIM_PATH "/sys/class/drm/card0-DSI-1/hbm_brightness"
#define DIM_AMOUNT_PATH "/sys/class/drm/card0-DSI-1/dim_alpha"
#define DC_DIM_PATH "/sys/class/drm/card0-DSI-1/dimlayer_bl_en"

#define NATIVE_DISPLAY_P3 "/sys/class/drm/card0-DSI-1/native_display_p3_mode"
#define NATIVE_DISPLAY_SRGB "/sys/class/drm/card0-DSI-1/native_display_customer_srgb_mode"
#define NATIVE_DISPLAY_NIGHT "/sys/class/drm/card0-DSI-1/night_mode"

#define NATIVE_DISPLAY_WIDE "/sys/class/drm/card0-DSI-1/native_display_wide_color_mode"

namespace vendor {
namespace lineage {
namespace biometrics {
namespace fingerprint {
namespace inscreen {
namespace V1_0 {
namespace implementation {

int wide,p3,srgb,night;
bool dcDimState;
int device[3] = {0, 0, 0};

using android::base::GetProperty;

/*
 * Write value to path and close file.
 */
template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;

    file >> result;
    return file.fail() ? def : result;
}

FingerprintInscreen::FingerprintInscreen() {
    this->mVendorFpService = IVendorFingerprintExtensions::getService();
    this->mVendorDisplayService = IOneplusDisplay::getService();
    std::string device = android::base::GetProperty("ro.product.device", "");
    device[0] = device == "OnePlus7" ? 1 : 0;
    device[1] = device == "OnePlus7pro" ? 1 : 0;
    device[2] = device == "OnePlus7T" ? 1 : 0;
    device[3] = device == "OnePlus7TPro" ? 1 : 0;
}

Return<void> FingerprintInscreen::onStartEnroll() {
    this->mVendorFpService->updateStatus(OP_DISABLE_FP_LONGPRESS);
    this->mVendorFpService->updateStatus(OP_RESUME_FP_ENROLL);

    return Void();
}

Return<void> FingerprintInscreen::onFinishEnroll() {
    this->mVendorFpService->updateStatus(OP_FINISH_FP_ENROLL);

    return Void();
}

Return<void> FingerprintInscreen::onPress() {
    this->mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 5);
    this->mVendorDisplayService->setMode(OP_DISPLAY_NOTIFY_PRESS, 1);
    set(HBM_ENABLE_PATH, 1);

    return Void();
}

Return<void> FingerprintInscreen::onRelease() {
    set(HBM_ENABLE_PATH, 0);
    this->mVendorDisplayService->setMode(OP_DISPLAY_NOTIFY_PRESS, 0);
    set(HBM_DIM_PATH, 255 - getDimAmount(255));

    return Void();
}

Return<void> FingerprintInscreen::onShowFODView() {
    this->mVendorDisplayService->setMode(OP_DISPLAY_AOD_MODE, 2);
    this->mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 1);
    wide = get(NATIVE_DISPLAY_WIDE, 0);
    p3 = get(NATIVE_DISPLAY_P3, 0);
    srgb = get(NATIVE_DISPLAY_SRGB, 0);
    night = get(NATIVE_DISPLAY_NIGHT, 0);
    dcDimState = get(DC_DIM_PATH, 0);

    set(DC_DIM_PATH, 0);
    set(NATIVE_DISPLAY_P3, 0);
    set(NATIVE_DISPLAY_SRGB, 0);
    set(NATIVE_DISPLAY_NIGHT, 0);
    set(NATIVE_DISPLAY_WIDE, 1);

    set(HBM_DIM_PATH, 255 - getDimAmount(255));

    return Void();
}

Return<void> FingerprintInscreen::onHideFODView() {
    set(HBM_ENABLE_PATH, 0);
    set(DC_DIM_PATH, dcDimState);
    set(NATIVE_DISPLAY_WIDE, 0);

    set(NATIVE_DISPLAY_WIDE, wide);
    set(NATIVE_DISPLAY_P3, p3);
    set(NATIVE_DISPLAY_SRGB, srgb);
    set(NATIVE_DISPLAY_NIGHT, night);

    this->mVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, 0);
    return Void();
}

Return<bool> FingerprintInscreen::handleAcquired(int32_t acquiredInfo, int32_t vendorCode) {
    std::lock_guard<std::mutex> _lock(mCallbackLock);
    if (mCallback == nullptr) {
        return false;
    }

    if (acquiredInfo == FINGERPRINT_ACQUIRED_VENDOR) {
        if (vendorCode == 0) {
            Return<void> ret = mCallback->onFingerDown();
            if (!ret.isOk()) {
                LOG(ERROR) << "FingerDown() error: " << ret.description();
            }
            return true;
        }

        if (vendorCode == 1) {
            Return<void> ret = mCallback->onFingerUp();
            if (!ret.isOk()) {
                LOG(ERROR) << "FingerUp() error: " << ret.description();
            }
            return true;
        }
    }

    return false;
}

Return<bool> FingerprintInscreen::handleError(int32_t error, int32_t vendorCode) {
    return error == FINGERPRINT_ERROR_VENDOR && vendorCode == 6;
}

Return<void> FingerprintInscreen::setLongPressEnabled(bool enabled) {
    this->mVendorFpService->updateStatus(
            enabled ? OP_ENABLE_FP_LONGPRESS : OP_DISABLE_FP_LONGPRESS);

    return Void();
}

Return<int32_t> FingerprintInscreen::getDimAmount(int32_t) {
    int dimAmount = get(DIM_AMOUNT_PATH, 0);
    LOG(INFO) << "dimAmount = " << dimAmount;

    return dimAmount;
}

Return<bool> FingerprintInscreen::shouldBoostBrightness() {
    if (device[0])
        return true;
    else
        return false;
}

Return<void> FingerprintInscreen::setCallback(const sp<IFingerprintInscreenCallback>& callback) {
    {
        std::lock_guard<std::mutex> _lock(mCallbackLock);
        mCallback = callback;
    }

    return Void();
}

Return<int32_t> FingerprintInscreen::getPositionX() {
    return FOD_POS_X;
}

Return<int32_t> FingerprintInscreen::getPositionY() {
    return FOD_POS_Y;
}

Return<int32_t> FingerprintInscreen::getSize() {
    return FOD_SIZE;
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace inscreen
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace lineage
}  // namespace vendor
