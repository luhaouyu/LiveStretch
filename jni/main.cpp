#include <jni.h>
#include <android/native_window.h>
#include <android/log.h>
#include <sys/sysmacros.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <thread>
#include <unistd.h>
#include <utility>
#include "zygisk.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "LiveStretch", __VA_ARGS__)
#define CFG_PATH "/data/local/tmp/stretch.cfg"
#define TARGET_PKG "com.tencent.igfit" // <-- GANTI PACKAGE NAME GAME DI SINI

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static std::atomic<float> g_ratio{1.0f};
static ANativeWindow* g_win = nullptr;
static int32_t g_origW = 0, g_origH = 0, g_origFmt = 0;

static int32_t (*orig_setBuffersGeometry)(ANativeWindow*, int32_t, int32_t, int32_t) = nullptr;

int32_t my_setBuffersGeometry(ANativeWindow* window, int32_t width, int32_t height, int32_t format) {
    if (window && width > 0 && height > 0) {
        g_win = window;
        g_origW = width;
        g_origH = height;
        g_origFmt = format;
    }
    float r = g_ratio.load();
    int32_t newW = (r >= 0.5f && r <= 2.5f) ? static_cast<int32_t>(width * r) : width;
    return orig_setBuffersGeometry(window, newW, height, format);
}

void configWatcherThread() {
    float lastApplied = 1.0f;
    while (true) {
        FILE* fp = fopen(CFG_PATH, "r");
        if (fp) {
            float val = 1.0f;
            if (fscanf(fp, "%f", &val) == 1) {
                if (val >= 0.5f && val <= 2.5f && std::fabs(val - lastApplied) > 0.005f) {
                    lastApplied = val;
                    g_ratio.store(val);
                    if (g_win && orig_setBuffersGeometry && g_origW > 0) {
                        int32_t newW = static_cast<int32_t>(g_origW * val);
                        orig_setBuffersGeometry(g_win, newW, g_origH, g_origFmt);
                        LOGD("Live stretch applied: %.2f (%dx%d)", val, newW, g_origH);
                    }
                }
            }
            fclose(fp);
        }
        usleep(200000);
    }
}

class LiveStretchModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        if (pkg && strcmp(pkg, TARGET_PKG) == 0) {
            isTarget = true;
        }
        if (pkg) env->ReleaseStringUTFChars(args->nice_name, pkg);

        if (!isTarget) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (!isTarget) return;

        // Scan /proc/self/maps untuk mendaftarkan PLT Hook (Zygisk API v4)
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp) {
            char line[512];
            std::set<std::pair<dev_t, ino_t>> hooked;
            while (fgets(line, sizeof(line), fp)) {
                unsigned int major, minor;
                ino_t inode;
                char perms[5], path[256] = {0};
                if (sscanf(line, "%*lx-%*lx %4s %*lx %x:%x %lu %255s",
                           perms, &major, &minor, &inode, path) >= 4) {
                    if (inode == 0 || strstr(path, ".so") == nullptr) continue;
                    dev_t dev = makedev(major, minor);
                    if (hooked.insert({dev, inode}).second) {
                        api->pltHookRegister(dev, inode, "ANativeWindow_setBuffersGeometry",
                                             reinterpret_cast<void*>(my_setBuffersGeometry),
                                             reinterpret_cast<void**>(&orig_setBuffersGeometry));
                    }
                }
            }
            fclose(fp);
            api->pltHookCommit();
        }

        std::thread(configWatcherThread).detach();
        LOGD("LiveStretch active for %s", TARGET_PKG);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool isTarget = false;
};

REGISTER_ZYGISK_MODULE(LiveStretchModule)
